"use strict";

// Transient research runner for detector attack/release behavior. It reuses the
// DC bridge solve at each control point and exports reproducible summaries.
const fs = require("node:fs");
const path = require("node:path");
const { solveDc, solutionObject } = require("./mna_core");
const { buildB6ParametricDcNetlist } = require("./b6_parametric");
const {
  detectorScenarios,
  simulateEnvelope,
} = require("./b11_s7_detector_parametric");

const CASES = Object.freeze([
  {
    name: "limiter_nominal_12vrms_scale1_r10k",
    detectorScenario: "limiter_nominal",
    ueHighRms: 12,
    releaseOhm: 10e3,
    commandSourceOhm: 10e3,
    detectorToDriveScale: 1,
    status: "BOUNDED_QUASI_STATIC",
  },
  {
    name: "limiter_nominal_12vrms_scale025_r10k",
    detectorScenario: "limiter_nominal",
    ueHighRms: 12,
    releaseOhm: 10e3,
    commandSourceOhm: 10e3,
    detectorToDriveScale: 0.25,
    status: "CALIBRATION_SENSITIVITY",
  },
  {
    name: "compressor_hypothesis_8vrms_scale1_r10k",
    detectorScenario: "compressor_s7_17_to_19_zener_bypassed",
    ueHighRms: 8,
    releaseOhm: 10e3,
    commandSourceOhm: 10e3,
    detectorToDriveScale: 1,
    status: "HYPOTHESE_S7_COMPRESSOR",
  },
]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function csvCell(value) {
  if (typeof value === "number") return Number.isFinite(value) ? String(value) : "";
  if (typeof value === "boolean") return value ? "true" : "false";
  if (value === undefined || value === null) return "";
  if (typeof value === "object") return JSON.stringify(value).replaceAll(",", ";");
  return String(value).replaceAll(",", ";");
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) lines.push(columns.map((column) => csvCell(row[column])).join(","));
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
}

function scenarioByName(name) {
  const scenario = detectorScenarios.find((item) => item.name === name);
  if (!scenario) throw new Error(`Unknown detector scenario: ${name}`);
  return scenario;
}

function bridgePoint(driveVolt, commandSourceOhm, previousSolution) {
  const netlist = buildB6ParametricDcNetlist(driveVolt, {
    bridgeDiodeLaw: "u273_empirical",
    commandDcMode: "thevenin",
    commandSourceVolt: driveVolt,
    commandSourceOhm,
    r7EffectiveOhm: 100,
    r8EffectiveOhm: 250e3,
  });
  const attempts = [
    { initialGuess: previousSolution, damping: 0.35, maxIterations: 260 },
    { initialGuess: previousSolution, damping: 0.6, maxIterations: 500 },
    { damping: 0.6, maxIterations: 500 },
    { damping: 1.0, maxIterations: 500 },
  ];
  let result = null;
  for (const attempt of attempts) {
    result = solveDc(netlist, {
      ...attempt,
      tolerance: 1e-10,
      residualTolerance: 1e-9,
    });
    if (result.converged) break;
  }
  if (!result.converged) {
    throw new Error(`Transient bridge point failed for drive=${driveVolt}`);
  }
  const solution = solutionObject(result, netlist);
  const cmdVolt = solution.CMD ?? 0;
  const b11DrvVolt = solution.B11_DRV ?? driveVolt;
  return {
    result,
    solution,
    cmdVolt,
    sourceCurrentAmp: (b11DrvVolt - cmdVolt) / commandSourceOhm,
  };
}

function timeAtCrossing(rows, predicate) {
  const row = rows.find(predicate);
  return row ? row.time : null;
}

function summarizeCase(caseDef, rows) {
  const maxCmd = Math.max(...rows.map((row) => row.cmdVolt));
  const onStart = 0.1;
  const onEnd = 1.1;
  const attack90 = maxCmd > 0
    ? timeAtCrossing(rows, (row) => row.time >= onStart && row.cmdVolt >= 0.9 * maxCmd)
    : null;
  const release10 = maxCmd > 0
    ? timeAtCrossing(rows, (row) => row.time >= onEnd && row.cmdVolt <= 0.1 * maxCmd)
    : null;
  return {
    case: caseDef.name,
    detectorScenario: caseDef.detectorScenario,
    status: caseDef.status,
    ueHighRms: caseDef.ueHighRms,
    releaseOhm: caseDef.releaseOhm,
    commandSourceOhm: caseDef.commandSourceOhm,
    detectorToDriveScale: caseDef.detectorToDriveScale,
    maxDriveVolt: Math.max(...rows.map((row) => row.driveVolt)),
    maxCmdVolt: maxCmd,
    attack90TimeSeconds: attack90,
    release10TimeSeconds: release10,
    boundary:
      "Quasi-static detector-to-bridge transient. C11/drive calibration is swept; this is not yet a full capacitor-companion MNA of B11+B6.",
  };
}

function runCase(caseDef) {
  const detector = scenarioByName(caseDef.detectorScenario);
  const envelope = simulateEnvelope(detector, caseDef.ueHighRms, caseDef.releaseOhm, {
    dt: 0.001,
    sampleEvery: 0.01,
    preSeconds: 0.1,
    onSeconds: 1.0,
    offSeconds: 2.0,
  });
  const rows = [];
  let previous = null;
  for (const row of envelope.rows) {
    const driveVolt = Math.max(0, row.vCapEquivalentVolt * caseDef.detectorToDriveScale);
    const point = bridgePoint(driveVolt, caseDef.commandSourceOhm, previous);
    previous = point.result.solution;
    rows.push({
      case: caseDef.name,
      detectorScenario: caseDef.detectorScenario,
      status: caseDef.status,
      time: row.time,
      ueRms: row.ueRms,
      detectorAverageCurrentAmp: row.averageCurrentAmp,
      vCapEquivalentVolt: row.vCapEquivalentVolt,
      detectorToDriveScale: caseDef.detectorToDriveScale,
      driveVolt,
      cmdVolt: point.cmdVolt,
      sourceCurrentAmp: point.sourceCurrentAmp,
      maxResidual: point.result.residual?.maxAbs ?? null,
    });
  }
  return { rows, summary: summarizeCase(caseDef, rows), detectorEnvelopeSummary: envelope.summary };
}

function runTransient() {
  const caseResults = CASES.map(runCase);
  const rows = caseResults.flatMap((item) => item.rows);
  const summaries = caseResults.map((item) => ({
    ...item.summary,
    detectorEnvelopeSummary: item.detectorEnvelopeSummary,
  }));

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const payload = {
    title: "Siemens U273 transient attack/release bounded reference",
    status: "BOUNDED_QUASI_STATIC_TRANSIENT",
    scientificBoundary:
      "Detector envelope is computed with the existing B11/S7 threshold model; B6 bridge is solved quasi-statically at each sampled command point. Full B11+B6 transient waits for C11-to-drive calibration and active BJT closure.",
    cases: CASES,
    summaries,
    rows,
  };

  const jsonPath = path.join(outDir, "u273_transient_attack_release.json");
  fs.writeFileSync(jsonPath, JSON.stringify(payload, null, 2));

  const csvPath = path.join(outDir, "u273_transient_attack_release.csv");
  writeCsv(csvPath, rows, [
    "case",
    "detectorScenario",
    "status",
    "time",
    "ueRms",
    "detectorAverageCurrentAmp",
    "vCapEquivalentVolt",
    "detectorToDriveScale",
    "driveVolt",
    "cmdVolt",
    "sourceCurrentAmp",
    "maxResidual",
  ]);

  return { jsonPath, csvPath, payload };
}

function run() {
  const { jsonPath, csvPath, payload } = runTransient();
  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  for (const summary of payload.summaries) {
    console.log(`${summary.case}: maxCMD=${summary.maxCmdVolt.toFixed(6)} V, attack90=${summary.attack90TimeSeconds}, release10=${summary.release10TimeSeconds}`);
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  CASES,
  bridgePoint,
  runCase,
  runTransient,
  run,
};
