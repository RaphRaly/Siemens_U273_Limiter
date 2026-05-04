"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
  V,
  passiveCurrents,
} = require("./b11_ts1_ts2_topology_constraints");

const CRITERIA = Object.freeze({
  strictVbeMin: 0.55,
  strictVbeMax: 0.75,
  softVbeMin: 0.45,
  softVbeMax: 0.85,
  strictVceMin: 0.40,
  softVceMin: 0.20,
  minPlausibleBeta: 10,
  maxPlausibleBeta: 500,
});

const NODE_SETS = Object.freeze({
  Ts1: [
    { node: "N215", voltage: V.N215, note: "R10/R11 junction, not necessarily a transistor pin" },
    { node: "N10", voltage: V.N10, note: "printed 10 V around Ts1/R11/C6" },
    { node: "N9", voltage: V.N9, note: "printed 9 V around Ts2/R16/R13" },
    { node: "N05", voltage: V.N05, note: "printed 0.5 V command/emitter region" },
  ],
  Ts2: [
    { node: "N145", voltage: V.N145, note: "printed 14.5 V around Ts2/R15" },
    { node: "N10", voltage: V.N10, note: "printed 10 V around Ts1/R11/C6" },
    { node: "N9", voltage: V.N9, note: "printed 9 V around Ts2/R16/R13" },
    { node: "N05", voltage: V.N05, note: "printed 0.5 V command region" },
  ],
});

const CE_PAIRS_TO_TEST = Object.freeze({
  Ts1: [
    { collector: "N10", emitter: "N05", type: "npn", reason: "common NPN-like reading: high node collector, low node emitter" },
    { collector: "N05", emitter: "N10", type: "pnp", reason: "inverse PNP-like reading: high node emitter, low node collector" },
  ],
  Ts2: [
    { collector: "N145", emitter: "N9", type: "npn", reason: "common NPN-like reading: 14.5 V collector, 9 V emitter" },
    { collector: "N9", emitter: "N145", type: "pnp", reason: "inverse PNP-like reading: 14.5 V emitter, 9 V collector" },
  ],
});

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function nodeVoltage(transistor, nodeName) {
  const found = NODE_SETS[transistor].find((item) => item.node === nodeName);
  if (!found) throw new Error(`Unknown node ${nodeName} for ${transistor}`);
  return found.voltage;
}

function combinations(items) {
  const out = [];
  for (const collector of items) {
    for (const base of items) {
      for (const emitter of items) {
        if (collector.node === base.node || collector.node === emitter.node || base.node === emitter.node) continue;
        out.push({ collector, base, emitter });
      }
    }
  }
  return out;
}

function activeCheck(type, vc, vb, ve) {
  const lowerType = type.toLowerCase();
  let driveVolt;
  let outputVolt;
  let bcReverseVolt;

  if (lowerType === "npn") {
    driveVolt = vb - ve;
    outputVolt = vc - ve;
    bcReverseVolt = vc - vb;
  } else if (lowerType === "pnp") {
    driveVolt = ve - vb;
    outputVolt = ve - vc;
    bcReverseVolt = vb - vc;
  } else {
    throw new Error("type must be npn or pnp");
  }

  const strictDrive = driveVolt >= CRITERIA.strictVbeMin && driveVolt <= CRITERIA.strictVbeMax;
  const softDrive = driveVolt >= CRITERIA.softVbeMin && driveVolt <= CRITERIA.softVbeMax;
  const strictOutput = outputVolt >= CRITERIA.strictVceMin;
  const softOutput = outputVolt >= CRITERIA.softVceMin;
  const reverseBc = bcReverseVolt > 0;

  const reasons = [];
  if (!softDrive) reasons.push(`base-emitter drive ${driveVolt.toFixed(3)} V outside soft window`);
  if (!softOutput) reasons.push(`output voltage ${outputVolt.toFixed(3)} V below soft active limit`);
  if (!reverseBc) reasons.push(`base-collector junction not reverse biased by voltage ordering`);

  return {
    driveVolt,
    outputVolt,
    bcReverseVolt,
    softActive: softDrive && softOutput && reverseBc,
    strictActive: strictDrive && strictOutput && reverseBc,
    reasons: reasons.join("; ") || "voltage ordering plausible",
  };
}

function enumerateKnownNodeRows() {
  const rows = [];
  for (const transistor of Object.keys(NODE_SETS)) {
    for (const combo of combinations(NODE_SETS[transistor])) {
      for (const type of ["npn", "pnp"]) {
        const check = activeCheck(type, combo.collector.voltage, combo.base.voltage, combo.emitter.voltage);
        rows.push({
          transistor,
          type,
          collector: combo.collector.node,
          base: combo.base.node,
          emitter: combo.emitter.node,
          vc: combo.collector.voltage,
          vb: combo.base.voltage,
          ve: combo.emitter.voltage,
          driveVolt: check.driveVolt,
          outputVolt: check.outputVolt,
          bcReverseVolt: check.bcReverseVolt,
          softActive: check.softActive,
          strictActive: check.strictActive,
          reasons: check.reasons,
        });
      }
    }
  }
  return rows;
}

function requiredBaseRange(type, vc, ve) {
  if (type === "npn") {
    return {
      softMin: ve + CRITERIA.softVbeMin,
      softMax: ve + CRITERIA.softVbeMax,
      strictMin: ve + CRITERIA.strictVbeMin,
      strictMax: ve + CRITERIA.strictVbeMax,
      outputSoftOk: vc - ve >= CRITERIA.softVceMin,
      outputStrictOk: vc - ve >= CRITERIA.strictVceMin,
    };
  }
  return {
    softMin: ve - CRITERIA.softVbeMax,
    softMax: ve - CRITERIA.softVbeMin,
    strictMin: ve - CRITERIA.strictVbeMax,
    strictMax: ve - CRITERIA.strictVbeMin,
    outputSoftOk: ve - vc >= CRITERIA.softVceMin,
    outputStrictOk: ve - vc >= CRITERIA.strictVceMin,
  };
}

function knownNodesInRange(transistor, min, max) {
  return NODE_SETS[transistor]
    .filter((item) => item.voltage >= min && item.voltage <= max)
    .map((item) => item.node)
    .join("|") || "";
}

function requiredBaseRows() {
  const rows = [];
  for (const transistor of Object.keys(CE_PAIRS_TO_TEST)) {
    for (const pair of CE_PAIRS_TO_TEST[transistor]) {
      const vc = nodeVoltage(transistor, pair.collector);
      const ve = nodeVoltage(transistor, pair.emitter);
      const range = requiredBaseRange(pair.type, vc, ve);
      rows.push({
        transistor,
        type: pair.type,
        collector: pair.collector,
        emitter: pair.emitter,
        vc,
        ve,
        softBaseMin: range.softMin,
        softBaseMax: range.softMax,
        strictBaseMin: range.strictMin,
        strictBaseMax: range.strictMax,
        outputSoftOk: range.outputSoftOk,
        outputStrictOk: range.outputStrictOk,
        knownSoftBaseCandidates: knownNodesInRange(transistor, range.softMin, range.softMax),
        knownStrictBaseCandidates: knownNodesInRange(transistor, range.strictMin, range.strictMax),
        reason: pair.reason,
      });
    }
  }
  return rows;
}

function kclRows() {
  const p = passiveCurrents();
  const ts1Ic = p.iR11;
  const ts1IeLow = p.n05DemandLow;
  const ts1IeHigh = p.n05DemandHigh;
  const ts1IbLow = ts1IeLow - ts1Ic;
  const ts1IbHigh = ts1IeHigh - ts1Ic;

  const ts2Ic = p.iR15;
  const ts2IeKnown = p.n9KnownDemand;
  const ts2IbKnown = ts2IeKnown - ts2Ic;

  return [
    {
      transistor: "Ts1",
      hypothesis: "isolated NPN-like C=N10 E=N05 with Ic=I_R11",
      icAmp: ts1Ic,
      ieLowAmp: ts1IeLow,
      ieHighAmp: ts1IeHigh,
      ibLowAmp: ts1IbLow,
      ibHighAmp: ts1IbHigh,
      betaLow: ts1IbLow > 0 ? ts1Ic / ts1IbLow : Infinity,
      betaHigh: ts1IbHigh > 0 ? ts1Ic / ts1IbHigh : Infinity,
      plausible: false,
      reason: "effective beta is far below any plausible forward-active BJT value",
    },
    {
      transistor: "Ts2",
      hypothesis: "isolated NPN-like C=N145 E=N9 with Ic=I_R15 and Ie=I_R16+I_R13",
      icAmp: ts2Ic,
      ieLowAmp: ts2IeKnown,
      ieHighAmp: ts2IeKnown,
      ibLowAmp: ts2IbKnown,
      ibHighAmp: ts2IbKnown,
      betaLow: ts2IbKnown > 0 ? ts2Ic / ts2IbKnown : Infinity,
      betaHigh: ts2IbKnown > 0 ? ts2Ic / ts2IbKnown : Infinity,
      plausible: false,
      reason: "known emitter demand is smaller than supply current; missing branch or wrong isolated assumption",
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
  const knownRows = enumerateKnownNodeRows();
  const baseRows = requiredBaseRows();
  const kcl = kclRows();
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b11_ts1_ts2_pin_hypothesis_solver.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 Ts1/Ts2 B-C-E hypothesis solver",
        status: "voltage and KCL constraint solver; not final pin extraction",
        criteria: CRITERIA,
        nodeSets: NODE_SETS,
        knownNodeHypotheses: knownRows,
        requiredBaseRows: baseRows,
        kclRows: kcl,
      },
      null,
      2
    )
  );

  const knownCsv = path.join(outDir, "b11_ts1_ts2_known_node_hypotheses.csv");
  writeCsv(knownCsv, knownRows, [
    "transistor",
    "type",
    "collector",
    "base",
    "emitter",
    "vc",
    "vb",
    "ve",
    "driveVolt",
    "outputVolt",
    "bcReverseVolt",
    "softActive",
    "strictActive",
    "reasons",
  ]);

  const baseCsv = path.join(outDir, "b11_ts1_ts2_required_base_ranges.csv");
  writeCsv(baseCsv, baseRows, [
    "transistor",
    "type",
    "collector",
    "emitter",
    "vc",
    "ve",
    "softBaseMin",
    "softBaseMax",
    "strictBaseMin",
    "strictBaseMax",
    "outputSoftOk",
    "outputStrictOk",
    "knownSoftBaseCandidates",
    "knownStrictBaseCandidates",
    "reason",
  ]);

  const kclCsv = path.join(outDir, "b11_ts1_ts2_kcl_hypothesis_checks.csv");
  writeCsv(kclCsv, kcl, [
    "transistor",
    "hypothesis",
    "icAmp",
    "ieLowAmp",
    "ieHighAmp",
    "ibLowAmp",
    "ibHighAmp",
    "betaLow",
    "betaHigh",
    "plausible",
    "reason",
  ]);

  const strictKnown = knownRows.filter((row) => row.strictActive);
  const softKnown = knownRows.filter((row) => row.softActive);
  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${knownCsv}`);
  console.log(`Wrote ${baseCsv}`);
  console.log(`Wrote ${kclCsv}`);
  console.log("");
  console.log(`Known-node strict-active hypotheses: ${strictKnown.length}`);
  console.log(`Known-node soft-active hypotheses: ${softKnown.length}`);
  for (const row of baseRows) {
    console.log(
      `${row.transistor} ${row.type} C=${row.collector} E=${row.emitter}: ` +
      `strict base ${row.strictBaseMin.toFixed(2)}..${row.strictBaseMax.toFixed(2)} V, ` +
      `known candidates=${row.knownStrictBaseCandidates || "none"}`
    );
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  CRITERIA,
  NODE_SETS,
  activeCheck,
  enumerateKnownNodeRows,
  requiredBaseRows,
  kclRows,
  run,
};
