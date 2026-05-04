"use strict";

// Global DC validation runner. It compares generated operating points with
// printed schematic targets while keeping uncertain nodes marked as pending.
const fs = require("node:fs");
const path = require("node:path");
const {
  buildNetlist,
  buildDcNetlist,
  solveOperatingPoint,
} = require("./u273_orchestrator");

const printedDcTargets = Object.freeze([
  { card: "B11", node: "N215", targetVolt: 21.5, status: "DEPENDS-ON-P0" },
  { card: "B11", node: "N145", targetVolt: 14.5, status: "DEPENDS-ON-P0" },
  { card: "B11", node: "N10", targetVolt: 10, status: "DEPENDS-ON-P0" },
  { card: "B11", node: "N9", targetVolt: 9, status: "DEPENDS-ON-P0" },
  { card: "B11", node: "N05", targetVolt: 0.5, status: "DEPENDS-ON-P0" },
  { card: "B6", node: "V22", targetVolt: 22, status: "DEPENDS-ON-P0" },
  { card: "B6", node: "V12", targetVolt: 12.2, status: "DEPENDS-ON-P0" },
  { card: "B6", node: "V64", targetVolt: 6.4, status: "DEPENDS-ON-P0" },
]);

const defaultDcTolerances = Object.freeze({
  highNodeRelative: 0.07,
  lowNodeAbsoluteVolt: 0.15,
  lowNodeThresholdVolt: 2,
});

const SCENARIOS = Object.freeze([
  {
    name: "delivery_linear_r3k_v1",
    commandSourceVolt: 1,
    commandSourceOhm: 3e3,
    note: "Low Thevenin output resistance bound tied to visible R1=3k context.",
  },
  {
    name: "delivery_linear_r10k_v1",
    commandSourceVolt: 1,
    commandSourceOhm: 10e3,
    note: "Moderate Thevenin output resistance reference.",
  },
  {
    name: "detector_r31_51k_v1",
    commandSourceVolt: 1,
    commandSourceOhm: 51e3,
    note: "High impedance detector-side bound using visible R31=51k context.",
  },
  {
    name: "delivery_linear_r10k_v3",
    commandSourceVolt: 3,
    commandSourceOhm: 10e3,
    note: "Same reference at stronger command drive.",
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

function summarizeDiodes(diodeInfo) {
  return diodeInfo.map((d) => ({
    name: d.name,
    anode: d.anode,
    cathode: d.cathode,
    vdVolt: d.vd,
    rawVdVolt: d.rawVd,
    idAmp: d.id,
    idMicroAmp: d.id * 1e6,
    gdSiemens: d.gd,
    rdOhm: d.rd,
    model: d.model,
    inStatedCurrentRange: d.inStatedCurrentRange,
  }));
}

function buildTheveninReferenceNetlist({
  driveVolt = 1,
  sourceOhm = 10e3,
  b6Options = {},
} = {}) {
  return buildDcNetlist({
    ...b6Options,
    commandSourceVolt: driveVolt,
    commandSourceOhm: sourceOhm,
    bridgeDiodeLaw: b6Options.bridgeDiodeLaw ?? "u273_empirical",
  });
}

function buildU273DcGlobalNetlist(options = {}) {
  const netlist = buildTheveninReferenceNetlist({
    driveVolt: options.driveVolt ?? options.commandSourceVolt ?? 1,
    sourceOhm: options.sourceOhm ?? options.commandSourceOhm ?? 10e3,
    b6Options: options.b6Options ?? {},
  });
  return {
    netlist,
    metadata: {
      mode: "theveninReference",
      status: "guarded",
      reason: "Full active B11+B6 DC netlist is blocked by unread BJT routes and incomplete B11 detector/regulator topology.",
    },
    guarded: [
      "B11 active regulator",
      "B6 Ts1/Ts3/Ts5/Ts6",
      "B11 Ts1/Ts2",
      "S6 non-delivery contact tables",
      "S7 compressor physical contact table",
    ],
  };
}

function validateDcAgainstPrintedVoltages(solution, targets = printedDcTargets, tolerances = defaultDcTolerances) {
  return targets.map((target) => {
    const actual = solution[target.node];
    if (actual === undefined || target.status === "DEPENDS-ON-P0") {
      return {
        ...target,
        actualVolt: actual ?? null,
        errorVolt: null,
        relativeError: null,
        validationStatus: "DEPENDS-ON-P0",
      };
    }

    const errorVolt = actual - target.targetVolt;
    const absError = Math.abs(errorVolt);
    const relativeError = absError / Math.abs(target.targetVolt);
    const limit = Math.abs(target.targetVolt) < tolerances.lowNodeThresholdVolt
      ? tolerances.lowNodeAbsoluteVolt
      : Math.abs(target.targetVolt) * tolerances.highNodeRelative;
    return {
      ...target,
      actualVolt: actual,
      errorVolt,
      relativeError,
      validationStatus: absError <= limit ? "PASS" : "FAIL",
    };
  });
}

function runScenario(scenario) {
  const solved = solveOperatingPoint({
    commandSourceVolt: scenario.commandSourceVolt,
    commandSourceOhm: scenario.commandSourceOhm,
  });
  const cmdVolt = solved.solution.CMD ?? 0;
  const b11DrvVolt = solved.solution.B11_DRV ?? scenario.commandSourceVolt;
  return {
    ...scenario,
    status: "THEVENIN_REFERENCE_NOT_FULL_ACTIVE_LOOP",
    converged: solved.result.converged,
    iterations: solved.result.iterations,
    residual: solved.result.residual,
    nodes: solved.solution,
    commandSagVolt: b11DrvVolt - cmdVolt,
    sourceCurrentAmp: (b11DrvVolt - cmdVolt) / scenario.commandSourceOhm,
    diodes: summarizeDiodes(solved.result.diodeInfo),
    printedVoltageValidation: validateDcAgainstPrintedVoltages(solved.solution),
  };
}

function nodeRows(results) {
  const rows = [];
  for (const result of results) {
    for (const [node, voltage] of Object.entries(result.nodes)) {
      rows.push({
        scenario: result.name,
        node,
        voltage,
        status: result.status,
      });
    }
    for (const diode of result.diodes) {
      rows.push({
        scenario: result.name,
        node: `${diode.name}:id_uA`,
        voltage: diode.idMicroAmp,
        status: diode.inStatedCurrentRange ? "diode_current_in_stated_range" : "diode_current_outside_stated_range",
      });
    }
  }
  return rows;
}

function runGlobalDc() {
  const machineNetlist = buildNetlist();
  const results = SCENARIOS.map(runScenario);
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const payload = {
    title: "Siemens U273 DC global reference solve",
    status: "PARAMETRIC_THEVENIN_REFERENCE",
    scientificBoundary:
      "This file starts Phase C but does not claim the full active B11+B6 loop. B11/S6/S7 is still represented by the finite Thevenin port from Etape 24.",
    validationPolicy:
      "Printed Siemens B11/B6 active-stage voltages are listed as DEPENDS-ON-P0 until unread BJT routes and full B11 netlist are closed.",
    machineNetlistVersion: machineNetlist.version,
    scenarios: SCENARIOS,
    results,
    pendingPrintedVoltageTargets: printedDcTargets,
  };

  const jsonPath = path.join(outDir, "u273_dc_global.json");
  fs.writeFileSync(jsonPath, JSON.stringify(payload, null, 2));

  const csvPath = path.join(outDir, "u273_dc_global_node_voltages.csv");
  writeCsv(csvPath, nodeRows(results), ["scenario", "node", "voltage", "status"]);

  return { jsonPath, csvPath, payload };
}

function solveU273DcGlobal(options = {}) {
  const guardedNetlist = buildU273DcGlobalNetlist(options);
  const solved = solveOperatingPoint({
    commandSourceVolt: options.driveVolt ?? options.commandSourceVolt ?? 1,
    commandSourceOhm: options.sourceOhm ?? options.commandSourceOhm ?? 10e3,
    ...(options.b6Options ?? {}),
  });
  return {
    global: null,
    theveninReference: {
      ...guardedNetlist,
      result: solved.result,
      solution: solved.solution,
      validation: validateDcAgainstPrintedVoltages(solved.solution),
    },
    warnings: guardedNetlist.guarded,
  };
}

function run() {
  const { jsonPath, csvPath, payload } = runGlobalDc();
  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  for (const result of payload.results) {
    console.log(
      `${result.name}: converged=${result.converged}, CMD=${(result.nodes.CMD ?? 0).toFixed(6)} V, ` +
      `Icmd=${(result.sourceCurrentAmp * 1e6).toFixed(3)} uA`
    );
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  printedDcTargets,
  defaultDcTolerances,
  SCENARIOS,
  buildU273DcGlobalNetlist,
  buildTheveninReferenceNetlist,
  solveU273DcGlobal,
  validateDcAgainstPrintedVoltages,
  runScenario,
  runGlobalDc,
  run,
};
