"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { smallSignalParams } = require("./bjt_small_signal");

const V = Object.freeze({
  V24: 24,
  N215: 21.5,
  N145: 14.5,
  N10: 10,
  N9: 9,
  N05: 0.5,
});

const R = Object.freeze({
  R10: 2e3,
  R11: 10e3,
  R12: 120,
  R13: 220e3,
  R15: 1.2e3,
  R16: 1.2e3,
  R7minPlusR8: 680,
  R7maxPlusR8: 1180,
  R9: 56e3,
});

const betaSweep = Object.freeze([50, 100, 200]);

function current(fromVolt, toVolt, resistanceOhm) {
  return (fromVolt - toVolt) / resistanceOhm;
}

function milli(value) {
  return value * 1e3;
}

function passiveCurrents() {
  const iR10 = current(V.V24, V.N215, R.R10);
  const iR11 = current(V.N215, V.N10, R.R11);
  const iR15 = current(V.V24, V.N145, R.R15);
  const iR16 = current(V.N9, 0, R.R16);
  const iR13 = current(V.N9, V.N05, R.R13);
  const iR12 = current(V.N05, 0, R.R12);
  const iR9 = current(V.N05, 0, R.R9);
  const iR7R8MinDemand = current(V.N05, 0, R.R7maxPlusR8);
  const iR7R8MaxDemand = current(V.N05, 0, R.R7minPlusR8);

  return {
    iR10,
    iR11,
    iR15,
    iR16,
    iR13,
    iR12,
    iR9,
    iR7R8MinDemand,
    iR7R8MaxDemand,
    n215ResidualToActive: iR10 - iR11,
    n9KnownDemand: iR16 + iR13,
    n05DemandLow: iR12 + iR9 + iR7R8MinDemand,
    n05DemandHigh: iR12 + iR9 + iR7R8MaxDemand,
  };
}

function isolatedBjtChecks() {
  const p = passiveCurrents();
  const ts1CollectorCandidate = p.iR11;
  const ts1EmitterLow = p.n05DemandLow;
  const ts1EmitterHigh = p.n05DemandHigh;
  const ts1BaseIfIsolatedLow = ts1EmitterLow - ts1CollectorCandidate;
  const ts1BaseIfIsolatedHigh = ts1EmitterHigh - ts1CollectorCandidate;

  const ts2CollectorCandidate = p.iR15;
  const ts2EmitterKnownDemand = p.n9KnownDemand;
  const ts2BaseIfIsolated = ts2EmitterKnownDemand - ts2CollectorCandidate;

  return {
    ts1: {
      topologyCandidate: "NPN-like local hypothesis: collector=N10, emitter=N05, base not printed/confirmed",
      collectorCandidateAmp: ts1CollectorCandidate,
      emitterDemandLowAmp: ts1EmitterLow,
      emitterDemandHighAmp: ts1EmitterHigh,
      baseCurrentIfIsolatedLowAmp: ts1BaseIfIsolatedLow,
      baseCurrentIfIsolatedHighAmp: ts1BaseIfIsolatedHigh,
      betaEffectiveLow: ts1BaseIfIsolatedLow > 0 ? ts1CollectorCandidate / ts1BaseIfIsolatedLow : Infinity,
      betaEffectiveHigh: ts1BaseIfIsolatedHigh > 0 ? ts1CollectorCandidate / ts1BaseIfIsolatedHigh : Infinity,
      conclusion: "An isolated BJT fed only by R11 cannot supply the N05 resistive demand; additional active paths or a different terminal interpretation are required.",
    },
    ts2: {
      topologyCandidate: "NPN-like local hypothesis: collector=N145, emitter=N9, base not printed/confirmed",
      collectorCandidateAmp: ts2CollectorCandidate,
      emitterKnownDemandAmp: ts2EmitterKnownDemand,
      baseCurrentIfIsolatedAmp: ts2BaseIfIsolated,
      conclusion: ts2BaseIfIsolated < 0
        ? "Known emitter demand is lower than R15 supply; the difference must leave through another branch or R15 is not purely collector current."
        : "Known currents are compatible in sign with a simple BJT but still require base/topology confirmation.",
    },
  };
}

function makeSmallSignalRows() {
  const p = passiveCurrents();
  const currents = [
    {
      name: "Ts1_R11_local_current",
      transistor: "B11/Ts1",
      currentAmp: p.iR11,
      status: "local collector-current candidate only",
      warning: "Not enough to satisfy N05 demand in isolated model.",
    },
    {
      name: "Ts1_N05_demand_low",
      transistor: "B11/Ts1",
      currentAmp: p.n05DemandLow,
      status: "emitter/load demand bound",
      warning: "Demand at N05, not confirmed collector current.",
    },
    {
      name: "Ts1_N05_demand_high",
      transistor: "B11/Ts1",
      currentAmp: p.n05DemandHigh,
      status: "emitter/load demand bound",
      warning: "Demand at N05, not confirmed collector current.",
    },
    {
      name: "Ts2_R15_supply",
      transistor: "B11/Ts2",
      currentAmp: p.iR15,
      status: "local supply current candidate",
      warning: "R15 current may feed transistor plus other branches.",
    },
    {
      name: "Ts2_N9_known_demand",
      transistor: "B11/Ts2",
      currentAmp: p.n9KnownDemand,
      status: "known emitter/load demand",
      warning: "R16+R13 current only.",
    },
  ];

  const rows = [];
  for (const item of currents) {
    for (const beta of betaSweep) {
      const params = smallSignalParams({ currentAmp: item.currentAmp, beta });
      rows.push({
        ...item,
        beta,
        gmSiemens: params.gmSiemens,
        reOhm: params.emitterDynamicOhm,
        rPiOhm: params.rPiOhm,
      });
    }
  }
  return rows;
}

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) {
    lines.push(columns.map((column) => row[column]).join(","));
  }
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
}

function run() {
  const p = passiveCurrents();
  const checks = isolatedBjtChecks();
  const smallSignalRows = makeSmallSignalRows();
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b11_ts1_ts2_topology_constraints.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 Ts1/Ts2 topology and KCL constraints",
        status: "constraint analysis, not final active netlist",
        nodeVoltages: V,
        resistorValues: R,
        passiveCurrents: p,
        isolatedBjtChecks: checks,
        smallSignalRows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b11_ts1_ts2_small_signal_bounds.csv");
  writeCsv(csvPath, smallSignalRows, [
    "name",
    "transistor",
    "status",
    "currentAmp",
    "beta",
    "gmSiemens",
    "reOhm",
    "rPiOhm",
    "warning",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  console.log(`R10 current: ${milli(p.iR10).toFixed(3)} mA`);
  console.log(`R11 current: ${milli(p.iR11).toFixed(3)} mA`);
  console.log(`N215 active residual: ${milli(p.n215ResidualToActive).toFixed(3)} mA`);
  console.log(`Ts1 N05 demand: ${milli(p.n05DemandLow).toFixed(3)}..${milli(p.n05DemandHigh).toFixed(3)} mA`);
  console.log(`Ts1 isolated beta effective: ${checks.ts1.betaEffectiveLow.toFixed(3)}..${checks.ts1.betaEffectiveHigh.toFixed(3)}`);
  console.log(`R15 current: ${milli(p.iR15).toFixed(3)} mA`);
  console.log(`N9 known demand: ${milli(p.n9KnownDemand).toFixed(3)} mA`);
  console.log(`Ts2 isolated base-current sign check: ${milli(checks.ts2.baseCurrentIfIsolatedAmp).toFixed(3)} mA`);
}

if (require.main === module) {
  run();
}

module.exports = {
  V,
  R,
  passiveCurrents,
  isolatedBjtChecks,
  makeSmallSignalRows,
  run,
};
