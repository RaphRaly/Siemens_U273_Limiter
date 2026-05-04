"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
  V,
  passiveCurrents,
} = require("./b11_ts1_ts2_topology_constraints");
const { smallSignalParams } = require("./bjt_small_signal");

const R = Object.freeze({
  R11: 10e3,
  R12: 120,
  R13: 220e3,
  R15: 1.2e3,
  R16: 1.2e3,
  R17: 6.8e3,
});

const betaSweep = Object.freeze([50, 100, 200]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function current(fromVolt, toVolt, resistanceOhm) {
  return (fromVolt - toVolt) / resistanceOhm;
}

function milli(value) {
  return value * 1e3;
}

function micro(value) {
  return value * 1e6;
}

function fixedCurrents() {
  const p = passiveCurrents();
  const iR15 = current(V.V24, V.N145, R.R15);
  const iR16Series = current(V.N145, V.N9, R.R16);
  const iTs2CollectorAvailable = iR15 - iR16Series;
  const iR11 = current(V.N215, V.N10, R.R11);
  const iR13 = current(V.N9, V.N05, R.R13);
  const iR12 = current(V.N05, 0, R.R12);
  const iR17 = current(V.N05, 0, R.R17);

  return {
    iR15,
    iR16Series,
    iTs2CollectorAvailable,
    iR11,
    iR13,
    iR12,
    iR17,
    n05DemandLow: p.n05DemandLow,
    n05DemandHigh: p.n05DemandHigh,
    n05DemandLowWithR17: p.n05DemandLow + iR17,
    n05DemandHighWithR17: p.n05DemandHigh + iR17,
  };
}

function pathClosureRows() {
  const f = fixedCurrents();
  const rows = [];
  for (const beta of betaSweep) {
    const ts2BaseCurrent = f.iTs2CollectorAvailable / beta;
    const ts2EmitterCurrent = f.iTs2CollectorAvailable + ts2BaseCurrent;
    const combinedIntoN05 = ts2EmitterCurrent + f.iR11 + f.iR13;
    const combinedWithoutTs2Base = f.iTs2CollectorAvailable + f.iR11 + f.iR13;
    const ts2Params = smallSignalParams({ currentAmp: f.iTs2CollectorAvailable, beta });
    rows.push({
      beta,
      r15CurrentAmp: f.iR15,
      r16SeriesCurrentAmp: f.iR16Series,
      ts2CollectorAvailableAmp: f.iTs2CollectorAvailable,
      ts2BaseCurrentAmp: ts2BaseCurrent,
      ts2EmitterCurrentAmp: ts2EmitterCurrent,
      r11CurrentAmp: f.iR11,
      r13CurrentAmp: f.iR13,
      combinedWithoutTs2BaseAmp: combinedWithoutTs2Base,
      combinedIntoN05Amp: combinedIntoN05,
      n05DemandLowAmp: f.n05DemandLow,
      n05DemandHighAmp: f.n05DemandHigh,
      residualLowAmp: f.n05DemandLow - combinedIntoN05,
      residualHighAmp: f.n05DemandHigh - combinedIntoN05,
      n05DemandLowWithR17Amp: f.n05DemandLowWithR17,
      n05DemandHighWithR17Amp: f.n05DemandHighWithR17,
      residualLowWithR17Amp: f.n05DemandLowWithR17 - combinedIntoN05,
      residualHighWithR17Amp: f.n05DemandHighWithR17 - combinedIntoN05,
      gmTs2Siemens: ts2Params.gmSiemens,
      reTs2Ohm: ts2Params.emitterDynamicOhm,
      rPiTs2Ohm: ts2Params.rPiOhm,
      verdict: "magnitude_plausible_pending_pin_confirmation",
      note: "Assumes the residual R15 current is Ts2 collector current and can be delivered toward the Ts1/N05 active path.",
    });
  }
  return rows;
}

function r7FitRows() {
  const f = fixedCurrents();
  return pathClosureRows().map((row) => {
    const requiredR7R8Current = row.combinedIntoN05Amp - f.iR12 - current(V.N05, 0, 56e3);
    const requiredR7R8Ohm = requiredR7R8Current > 0 ? V.N05 / requiredR7R8Current : Infinity;
    return {
      beta: row.beta,
      combinedIntoN05Amp: row.combinedIntoN05Amp,
      fixedR12R9DemandAmp: f.iR12 + current(V.N05, 0, 56e3),
      requiredR7R8CurrentAmp: requiredR7R8Current,
      requiredR7R8Ohm,
      knownR7R8MinOhm: 680,
      knownR7R8MaxOhm: 1180,
      withinIdealRange: requiredR7R8Ohm >= 680 && requiredR7R8Ohm <= 1180,
      distanceToRangeOhm: requiredR7R8Ohm > 1180 ? requiredR7R8Ohm - 1180 : requiredR7R8Ohm < 680 ? 680 - requiredR7R8Ohm : 0,
      note: "Compares the path current with the R7/R8 setting required to make N05 exact.",
    };
  });
}

function topologyAuditRows() {
  const f = fixedCurrents();
  return [
    {
      item: "R16_corrected_topology",
      oldAssumption: "N9_to_reference",
      correctedAssumption: "N145_to_N9",
      oldCurrentAmp: current(V.N9, 0, R.R16),
      correctedCurrentAmp: f.iR16Series,
      reason: "The crop shows R16 between the printed 14.5 V and 9 V nodes; KCL becomes coherent only with this topology.",
    },
    {
      item: "Ts2_active_residual",
      oldAssumption: "R15 entirely assigned to Ts2 or R16 treated separately from N145",
      correctedAssumption: "I_Ts2_C ~= I_R15 - I_R16(N145->N9)",
      oldCurrentAmp: f.iR15,
      correctedCurrentAmp: f.iTs2CollectorAvailable,
      reason: "R15 current splits at N145 between R16 and the active Ts2 branch.",
    },
  ];
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) {
    lines.push(columns.map((column) => row[column]).join(","));
  }
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
}

function run() {
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const fixed = fixedCurrents();
  const closure = pathClosureRows();
  const fit = r7FitRows();
  const audit = topologyAuditRows();

  const jsonPath = path.join(outDir, "b11_ts2_r15_to_n05_path_solver.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 Ts2/R15 to Ts1/N05 active-path solver",
        status: "current-path magnitude proof; pin extraction still pending",
        nodeVoltages: {
          V24: V.V24,
          N145: V.N145,
          N9: V.N9,
          N10: V.N10,
          N05: V.N05,
        },
        resistorValues: R,
        fixedCurrents: fixed,
        topologyAuditRows: audit,
        pathClosureRows: closure,
        r7FitRows: fit,
      },
      null,
      2
    )
  );

  const closureCsv = path.join(outDir, "b11_ts2_r15_to_n05_path_closure.csv");
  writeCsv(closureCsv, closure, [
    "beta",
    "r15CurrentAmp",
    "r16SeriesCurrentAmp",
    "ts2CollectorAvailableAmp",
    "ts2BaseCurrentAmp",
    "ts2EmitterCurrentAmp",
    "r11CurrentAmp",
    "r13CurrentAmp",
    "combinedWithoutTs2BaseAmp",
    "combinedIntoN05Amp",
    "n05DemandLowAmp",
    "n05DemandHighAmp",
    "residualLowAmp",
    "residualHighAmp",
    "n05DemandLowWithR17Amp",
    "n05DemandHighWithR17Amp",
    "residualLowWithR17Amp",
    "residualHighWithR17Amp",
    "gmTs2Siemens",
    "reTs2Ohm",
    "rPiTs2Ohm",
    "verdict",
    "note",
  ]);

  const fitCsv = path.join(outDir, "b11_ts2_r15_to_n05_r7_fit.csv");
  writeCsv(fitCsv, fit, [
    "beta",
    "combinedIntoN05Amp",
    "fixedR12R9DemandAmp",
    "requiredR7R8CurrentAmp",
    "requiredR7R8Ohm",
    "knownR7R8MinOhm",
    "knownR7R8MaxOhm",
    "withinIdealRange",
    "distanceToRangeOhm",
    "note",
  ]);

  const auditCsv = path.join(outDir, "b11_ts2_r15_to_n05_topology_audit.csv");
  writeCsv(auditCsv, audit, [
    "item",
    "oldAssumption",
    "correctedAssumption",
    "oldCurrentAmp",
    "correctedCurrentAmp",
    "reason",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${closureCsv}`);
  console.log(`Wrote ${fitCsv}`);
  console.log(`Wrote ${auditCsv}`);
  console.log("");
  console.log(`R15 current: ${milli(fixed.iR15).toFixed(3)} mA`);
  console.log(`R16 corrected N145->N9 current: ${milli(fixed.iR16Series).toFixed(3)} mA`);
  console.log(`Ts2 active collector residual: ${milli(fixed.iTs2CollectorAvailable).toFixed(3)} mA`);
  console.log(`R11 contribution: ${milli(fixed.iR11).toFixed(3)} mA`);
  console.log(`R13 contribution: ${micro(fixed.iR13).toFixed(3)} uA`);
  for (const row of closure) {
    console.log(
      `beta=${row.beta}: path current=${milli(row.combinedIntoN05Amp).toFixed(3)} mA, ` +
      `N05 residual=${micro(row.residualLowAmp).toFixed(1)}..${micro(row.residualHighAmp).toFixed(1)} uA`
    );
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  R,
  fixedCurrents,
  pathClosureRows,
  r7FitRows,
  topologyAuditRows,
  run,
};
