"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
  V,
  passiveCurrents,
} = require("./b11_ts1_ts2_topology_constraints");
const { smallSignalParams } = require("./bjt_small_signal");

const R = Object.freeze({
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

function n05RequirementRows() {
  const p = passiveCurrents();
  const r17Current = current(V.N05, 0, R.R17);
  return [
    {
      scenario: "confirmed_dc_without_R14C6_and_without_R17",
      requiredActiveCurrentLowAmp: p.n05DemandLow,
      requiredActiveCurrentHighAmp: p.n05DemandHigh,
      includedLoads: "R12+R9+R7/R8",
      note: "C6 is open at DC, so R14-C6 contributes zero DC current.",
    },
    {
      scenario: "confirmed_dc_without_R14C6_with_R17_candidate",
      requiredActiveCurrentLowAmp: p.n05DemandLow + r17Current,
      requiredActiveCurrentHighAmp: p.n05DemandHigh + r17Current,
      includedLoads: "R12+R9+R7/R8+R17(candidate)",
      note: "Adds R17 only as a candidate if its endpoint to reference is confirmed.",
    },
  ];
}

function bjtCurrentRows() {
  const rows = [];
  for (const req of n05RequirementRows()) {
    for (const beta of betaSweep) {
      for (const point of [
        { label: "low", emitterCurrentAmp: req.requiredActiveCurrentLowAmp },
        { label: "high", emitterCurrentAmp: req.requiredActiveCurrentHighAmp },
      ]) {
        const baseCurrentAmp = point.emitterCurrentAmp / (beta + 1);
        const collectorCurrentAmp = point.emitterCurrentAmp - baseCurrentAmp;
        const params = smallSignalParams({ currentAmp: collectorCurrentAmp, beta });
        rows.push({
          scenario: req.scenario,
          point: point.label,
          beta,
          emitterCurrentAmp: point.emitterCurrentAmp,
          collectorCurrentAmp,
          baseCurrentAmp,
          gmSiemens: params.gmSiemens,
          reOhm: params.emitterDynamicOhm,
          rPiOhm: params.rPiOhm,
          note: "If a single forward-active BJT supplies N05 as emitter current; topology still not fixed.",
        });
      }
    }
  }
  return rows;
}

function supplyComparisonRows() {
  const p = passiveCurrents();
  const rows = [];
  for (const req of n05RequirementRows()) {
    rows.push({
      scenario: req.scenario,
      candidateSupply: "R11_into_N10",
      supplyAmp: p.iR11,
      requiredLowAmp: req.requiredActiveCurrentLowAmp,
      requiredHighAmp: req.requiredActiveCurrentHighAmp,
      deficitLowAmp: req.requiredActiveCurrentLowAmp - p.iR11,
      deficitHighAmp: req.requiredActiveCurrentHighAmp - p.iR11,
      verdict: "R11_alone_insufficient_for_N05_DC",
      note: "R11 can be part of the bias network, but cannot alone be the collector supply for the N05 current.",
    });
    rows.push({
      scenario: req.scenario,
      candidateSupply: "R15_into_N145",
      supplyAmp: p.iR15,
      requiredLowAmp: req.requiredActiveCurrentLowAmp,
      requiredHighAmp: req.requiredActiveCurrentHighAmp,
      deficitLowAmp: req.requiredActiveCurrentLowAmp - p.iR15,
      deficitHighAmp: req.requiredActiveCurrentHighAmp - p.iR15,
      verdict: "R15_has_enough_magnitude_but_topology_unconfirmed",
      note: "R15 has enough current magnitude, but it belongs to the Ts2/N145 side and cannot be assigned without pin/topology proof.",
    });
  }
  return rows;
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

  const requirementRows = n05RequirementRows();
  const bjtRows = bjtCurrentRows();
  const supplyRows = supplyComparisonRows();

  const jsonPath = path.join(outDir, "b11_n05_active_requirement.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 N05 active DC current requirement",
        status: "constraint analysis after R14-C6 DC correction",
        nodeVoltages: {
          N05: V.N05,
          N10: V.N10,
          N145: V.N145,
        },
        requirementRows,
        bjtRows,
        supplyRows,
      },
      null,
      2
    )
  );

  const reqCsv = path.join(outDir, "b11_n05_active_dc_requirement.csv");
  writeCsv(reqCsv, requirementRows, [
    "scenario",
    "requiredActiveCurrentLowAmp",
    "requiredActiveCurrentHighAmp",
    "includedLoads",
    "note",
  ]);

  const bjtCsv = path.join(outDir, "b11_n05_active_bjt_small_signal.csv");
  writeCsv(bjtCsv, bjtRows, [
    "scenario",
    "point",
    "beta",
    "emitterCurrentAmp",
    "collectorCurrentAmp",
    "baseCurrentAmp",
    "gmSiemens",
    "reOhm",
    "rPiOhm",
    "note",
  ]);

  const supplyCsv = path.join(outDir, "b11_n05_supply_comparison.csv");
  writeCsv(supplyCsv, supplyRows, [
    "scenario",
    "candidateSupply",
    "supplyAmp",
    "requiredLowAmp",
    "requiredHighAmp",
    "deficitLowAmp",
    "deficitHighAmp",
    "verdict",
    "note",
  ]);

  const nominal = requirementRows[0];
  const beta100 = bjtRows.filter((row) => row.scenario === nominal.scenario && row.beta === 100);
  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${reqCsv}`);
  console.log(`Wrote ${bjtCsv}`);
  console.log(`Wrote ${supplyCsv}`);
  console.log("");
  console.log(
    `N05 active DC current required: ` +
    `${milli(nominal.requiredActiveCurrentLowAmp).toFixed(3)}..${milli(nominal.requiredActiveCurrentHighAmp).toFixed(3)} mA`
  );
  for (const row of beta100) {
    console.log(
      `beta=100 ${row.point}: Ic=${milli(row.collectorCurrentAmp).toFixed(3)} mA, ` +
      `Ib=${milli(row.baseCurrentAmp).toFixed(3)} mA, ` +
      `gm=${(row.gmSiemens * 1e3).toFixed(3)} mS, re=${row.reOhm.toFixed(3)} ohm`
    );
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  n05RequirementRows,
  bjtCurrentRows,
  supplyComparisonRows,
  run,
};
