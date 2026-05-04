"use strict";

// Global AC sweep runner for the generated U273 research netlist. Outputs are
// written as pinned JSON/CSV files consumed by reports and C++ tests.
const fs = require("node:fs");
const path = require("node:path");
const { solveDc } = require("./mna_core");
const { c, abs, solveAc } = require("./complex_ac");
const {
  buildB6ParametricDcNetlist,
  buildB6ParametricAcNetlist,
  buildB6CompleteSchematicInventory,
  diodeConductanceMap,
} = require("./b6_parametric");

const SOURCE_SCENARIOS = Object.freeze([
  { name: "rsource_3k", commandSourceOhm: 3e3, note: "low B11/S6/S7 output-impedance bound" },
  { name: "rsource_10k", commandSourceOhm: 10e3, note: "moderate B11/S6/S7 reference" },
  { name: "rsource_51k", commandSourceOhm: 51e3, note: "detector-side R31 bound" },
  { name: "rsource_100k", commandSourceOhm: 100e3, note: "high output-impedance sensitivity bound" },
]);

const DRIVE_VOLTS = Object.freeze([0, 1, 3, 8]);
const FREQUENCIES = Object.freeze([40, 100, 1000, 5000, 15000]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function phaseDeg(value) {
  return Math.atan2(value.im, value.re) * 180 / Math.PI;
}

function db20(value) {
  if (value <= 0) return -Infinity;
  return 20 * Math.log10(value);
}

function serializeComplex(value) {
  return {
    re: value.re,
    im: value.im,
    mag: abs(value),
    db: db20(abs(value)),
    phaseDeg: phaseDeg(value),
  };
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

function solveOperatingPoints(scenario, driveVolts = DRIVE_VOLTS) {
  const out = new Map();
  let previous = null;
  for (const driveVolt of driveVolts) {
    const dcNetlist = buildB6ParametricDcNetlist(driveVolt, {
      bridgeDiodeLaw: "u273_empirical",
      commandDcMode: "thevenin",
      commandSourceVolt: driveVolt,
      commandSourceOhm: scenario.commandSourceOhm,
      r7EffectiveOhm: 100,
      r8EffectiveOhm: 250e3,
    });
    const result = solveDc(dcNetlist, {
      initialGuess: previous,
      damping: 0.35,
      tolerance: 1e-10,
      residualTolerance: 1e-9,
      maxIterations: 260,
    });
    if (!result.converged) {
      throw new Error(`${scenario.name}: DC operating point failed at ${driveVolt} V`);
    }
    previous = result.solution;
    const nodes = Object.fromEntries(result.nodes.map((node, idx) => [node, result.solution[idx]]));
    out.set(driveVolt, {
      driveVolt,
      nodes,
      diodeInfo: result.diodeInfo,
      diodeConductance: diodeConductanceMap(result.diodeInfo),
      residual: result.residual,
    });
  }
  return out;
}

function makeRows() {
  const rows = [];
  const operatingPoints = {};

  for (const scenario of SOURCE_SCENARIOS) {
    const ops = solveOperatingPoints(scenario);
    operatingPoints[scenario.name] = Object.fromEntries(ops);

    for (const driveVolt of DRIVE_VOLTS) {
      const op = ops.get(driveVolt);
      for (const frequency of FREQUENCIES) {
        const acNetlist = buildB6ParametricAcNetlist(frequency, op.diodeConductance, {
          bridgeDiodeLaw: "u273_empirical",
          commandAcMode: "finite",
          zcmdOhm: scenario.commandSourceOhm,
          r7EffectiveOhm: 100,
          r8EffectiveOhm: 250e3,
          rAmpInputOhm: Infinity,
        });
        const ac = solveAc(acNetlist);
        const solution = ac.solution;
        const vna = solution.NA || c(0, 0);
        const vnb = solution.NB || c(0, 0);
        const vcmd = solution.CMD || c(0, 0);
        const ivac = solution["I(VAC)"] || c(0, 0);
        rows.push({
          scenario: scenario.name,
          scenarioNote: scenario.note,
          status: "BRIDGE_AND_COMMAND_SMALL_SIGNAL_REFERENCE",
          driveVolt,
          commandSourceOhm: scenario.commandSourceOhm,
          cmdDcVolt: op.nodes.CMD ?? 0,
          frequency,
          bjtStampMode: "NONE_GUARDED",
          scientificBoundary:
            "B6 full schematic inventory is closed, but active BJT AC stamps are guarded until conditional hypotheses are selected.",
          allDiodesInStatedRange: op.diodeInfo.every((d) => d.inStatedCurrentRange),
          anyDiodeOutOfStatedRange: op.diodeInfo.some((d) => !d.inStatedCurrentRange),
          maxResidual: op.residual?.maxAbs ?? null,
          VNA: serializeComplex(vna),
          VNB: serializeComplex(vnb),
          VCMD: serializeComplex(vcmd),
          IVAC: serializeComplex(ivac),
          inputImpedanceOhm: abs(ivac) > 0 ? 1 / abs(ivac) : Infinity,
        });
      }
    }
  }

  return { rows, operatingPoints };
}

function runGlobalAc() {
  const inventory = buildB6CompleteSchematicInventory({ bridgeDiodeLaw: "u273_empirical" });
  const { rows, operatingPoints } = makeRows();
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const payload = {
    title: "Siemens U273 AC global small-signal reference",
    status: "PARAMETRIC_THEVENIN_B6_BRIDGE_REFERENCE",
    scientificBoundary:
      "This is the global AC chain currently justified by the schematic: B11/S6/S7 finite command port + B6 bridge/input small-signal. Full BJT output AC requires conditional stamps and remains guarded.",
    frequencies: FREQUENCIES,
    driveVolts: DRIVE_VOLTS,
    sourceScenarios: SOURCE_SCENARIOS,
    b6InventoryStatus: inventory.status,
    operatingPoints,
    rows,
  };

  const jsonPath = path.join(outDir, "u273_ac_global.json");
  fs.writeFileSync(jsonPath, JSON.stringify(payload, null, 2));

  const csvPath = path.join(outDir, "u273_ac_gain_phase.csv");
  writeCsv(csvPath, rows.map((row) => ({
    scenario: row.scenario,
    drive_v: row.driveVolt,
    cmd_dc_v: row.cmdDcVolt,
    source_ohm: row.commandSourceOhm,
    frequency_hz: row.frequency,
    bjt_stamp_mode: row.bjtStampMode,
    all_diodes_in_range: row.allDiodesInStatedRange,
    any_diode_out_of_range: row.anyDiodeOutOfStatedRange,
    max_residual: row.maxResidual,
    VNA_mag: row.VNA.mag,
    VNA_db: row.VNA.db,
    VNA_phase_deg: row.VNA.phaseDeg,
    VNB_mag: row.VNB.mag,
    VNB_db: row.VNB.db,
    VCMD_mag: row.VCMD.mag,
    input_impedance_ohm: row.inputImpedanceOhm,
  })), [
    "scenario",
    "drive_v",
    "cmd_dc_v",
    "source_ohm",
    "frequency_hz",
    "bjt_stamp_mode",
    "all_diodes_in_range",
    "any_diode_out_of_range",
    "max_residual",
    "VNA_mag",
    "VNA_db",
    "VNA_phase_deg",
    "VNB_mag",
    "VNB_db",
    "VCMD_mag",
    "input_impedance_ohm",
  ]);

  return { jsonPath, csvPath, payload };
}

function run() {
  const { jsonPath, csvPath, payload } = runGlobalAc();
  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  const at1k = payload.rows.filter((row) => row.frequency === 1000 && row.driveVolt === 3);
  for (const row of at1k) {
    console.log(`${row.scenario}: VNA@1k=${row.VNA.mag.toExponential(6)} (${row.VNA.db.toFixed(2)} dB)`);
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  SOURCE_SCENARIOS,
  DRIVE_VOLTS,
  FREQUENCIES,
  solveOperatingPoints,
  makeRows,
  runGlobalAc,
  run,
};
