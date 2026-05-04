"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
  V,
  R: previousR,
  passiveCurrents,
} = require("./b11_ts1_ts2_topology_constraints");
const { requiredBaseRows } = require("./b11_ts1_ts2_pin_hypothesis_solver");

const R = Object.freeze({
  ...previousR,
  R14: 2e3,
  R17: 6.8e3,
});

const C = Object.freeze({
  C10: 100e-12,
});

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function current(fromVolt, toVolt, resistanceOhm) {
  return (fromVolt - toVolt) / resistanceOhm;
}

function milli(value) {
  return value * 1e3;
}

function formatMilli(value) {
  return `${milli(value).toFixed(3)} mA`;
}

function rangeCurrent(fromMinVolt, fromMaxVolt, toVolt, resistanceOhm) {
  const a = current(fromMinVolt, toVolt, resistanceOhm);
  const b = current(fromMaxVolt, toVolt, resistanceOhm);
  return {
    lowAmp: Math.min(a, b),
    highAmp: Math.max(a, b),
    centerAmp: (a + b) / 2,
  };
}

function classifyResidual(lowAmp, highAmp) {
  if (lowAmp <= 0 && highAmp >= 0) {
    return "passive_balance_possible_within_ranges";
  }
  if (highAmp < 0) {
    return "r14_supply_exceeds_N05_demand_requires_sink_or_wrong_endpoint";
  }
  if (lowAmp > 0 && highAmp <= 1e-3) {
    return "small_active_topup_needed";
  }
  return "large_active_topup_needed";
}

function n05DemandScenarios() {
  const p = passiveCurrents();
  const iR17ToGround = current(V.N05, 0, R.R17);
  return [
    {
      scenario: "baseline_without_R17",
      demandLowAmp: p.n05DemandLow,
      demandHighAmp: p.n05DemandHigh,
      note: "Same N05 load set as step 7: R12 + R9 + R7/R8.",
    },
    {
      scenario: "with_R17_to_ground",
      demandLowAmp: p.n05DemandLow + iR17ToGround,
      demandHighAmp: p.n05DemandHigh + iR17ToGround,
      note: "Adds R17=6.8k as a provisional N05-to-ground branch if the visual read is confirmed.",
    },
  ];
}

function strictBaseTargets() {
  return requiredBaseRows().map((row) => ({
    target: `${row.transistor}_${row.type}_base_required`,
    voltageLow: row.strictBaseMin,
    voltageHigh: row.strictBaseMax,
    source: `from ${row.transistor} ${row.type} C=${row.collector} E=${row.emitter}`,
    note: "Required strict base-emitter forward-bias window from step 8.",
  }));
}

function r14EndpointTargets() {
  return [
    {
      target: "N10_printed",
      voltageLow: V.N10,
      voltageHigh: V.N10,
      source: "printed 10 V node",
      note: "Best visual candidate for R14 right endpoint in the new crop; still treated as a candidate until source tracing is complete.",
    },
    {
      target: "N9_printed",
      voltageLow: V.N9,
      voltageHigh: V.N9,
      source: "printed 9 V node",
      note: "Alternative if the right endpoint is misread as the lower Ts2 node.",
    },
    {
      target: "N145_printed",
      voltageLow: V.N145,
      voltageHigh: V.N145,
      source: "printed 14.5 V node",
      note: "Stress test only; this would inject a large current into N05.",
    },
    ...strictBaseTargets(),
  ];
}

function r14BalanceRows() {
  const rows = [];
  for (const target of r14EndpointTargets()) {
    const supply = rangeCurrent(target.voltageLow, target.voltageHigh, V.N05, R.R14);
    for (const demand of n05DemandScenarios()) {
      const residualLowAmp = demand.demandLowAmp - supply.highAmp;
      const residualHighAmp = demand.demandHighAmp - supply.lowAmp;
      rows.push({
        endpointCandidate: target.target,
        endpointSource: target.source,
        demandScenario: demand.scenario,
        endpointVoltageLow: target.voltageLow,
        endpointVoltageHigh: target.voltageHigh,
        r14CurrentIntoN05LowAmp: supply.lowAmp,
        r14CurrentIntoN05HighAmp: supply.highAmp,
        n05DemandLowAmp: demand.demandLowAmp,
        n05DemandHighAmp: demand.demandHighAmp,
        residualN05DemandLowAmp: residualLowAmp,
        residualN05DemandHighAmp: residualHighAmp,
        classification: classifyResidual(residualLowAmp, residualHighAmp),
        note: `${target.note} Demand case: ${demand.note}`,
      });
    }
  }
  return rows;
}

function n10KclRows() {
  const p = passiveCurrents();
  const iR14N10ToN05 = current(V.N10, V.N05, R.R14);
  const residualLeavingN10 = iR14N10ToN05 - p.iR11;
  return [
    {
      node: "N10",
      assumption: "R14 connects N10 to N05",
      iR11IntoN10Amp: p.iR11,
      iR14LeavingN10Amp: iR14N10ToN05,
      residualActiveCurrentIntoN10Amp: residualLeavingN10,
      note: "If R14=N10->N05, R11 alone cannot feed R14; Ts1 or another active branch must supply the residual into N10.",
    },
  ];
}

function compensationRows() {
  return [
    { name: "R14_C10_order", resistanceOhm: R.R14, capacitanceFarad: C.C10 },
    { name: "R17_C10_order", resistanceOhm: R.R17, capacitanceFarad: C.C10 },
    { name: "R13_C10_order", resistanceOhm: R.R13, capacitanceFarad: C.C10 },
  ].map((row) => {
    const tauSecond = row.resistanceOhm * row.capacitanceFarad;
    const cornerHz = 1 / (2 * Math.PI * tauSecond);
    return {
      ...row,
      tauSecond,
      cornerHz,
      note: "Order-of-magnitude only; final C10 stamp needs confirmed endpoints.",
    };
  });
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

  const balanceRows = r14BalanceRows();
  const n10Rows = n10KclRows();
  const capRows = compensationRows();

  const jsonPath = path.join(outDir, "b11_ts1_ts2_hidden_node_balance.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 Ts1/Ts2 hidden-node and R14 balance check",
        status: "direct-R14 stress test only; superseded for DC if C6 is in series",
        nodeVoltages: V,
        resistorValues: R,
        capacitorValues: C,
        r14BalanceRows: balanceRows,
        n10KclRows: n10Rows,
        compensationRows: capRows,
      },
      null,
      2
    )
  );

  const balanceCsv = path.join(outDir, "b11_r14_n05_balance_candidates.csv");
  writeCsv(balanceCsv, balanceRows, [
    "endpointCandidate",
    "endpointSource",
    "demandScenario",
    "endpointVoltageLow",
    "endpointVoltageHigh",
    "r14CurrentIntoN05LowAmp",
    "r14CurrentIntoN05HighAmp",
    "n05DemandLowAmp",
    "n05DemandHighAmp",
    "residualN05DemandLowAmp",
    "residualN05DemandHighAmp",
    "classification",
    "note",
  ]);

  const n10Csv = path.join(outDir, "b11_n10_r14_kcl_candidate.csv");
  writeCsv(n10Csv, n10Rows, [
    "node",
    "assumption",
    "iR11IntoN10Amp",
    "iR14LeavingN10Amp",
    "residualActiveCurrentIntoN10Amp",
    "note",
  ]);

  const capCsv = path.join(outDir, "b11_c10_compensation_orders.csv");
  writeCsv(capCsv, capRows, [
    "name",
    "resistanceOhm",
    "capacitanceFarad",
    "tauSecond",
    "cornerHz",
    "note",
  ]);

  const n10RowsForPrint = balanceRows.filter(
    (row) => row.endpointCandidate === "N10_printed" && row.demandScenario === "with_R17_to_ground"
  );
  const ts1BaseRowsForPrint = balanceRows.filter(
    (row) => row.endpointCandidate === "Ts1_npn_base_required" && row.demandScenario === "with_R17_to_ground"
  );

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${balanceCsv}`);
  console.log(`Wrote ${n10Csv}`);
  console.log(`Wrote ${capCsv}`);
  console.log("");
  for (const row of n10RowsForPrint) {
    console.log(
      `Direct R14=N10->N05 stress test: I_R14=${formatMilli(row.r14CurrentIntoN05LowAmp)}; ` +
      `N05 residual=${formatMilli(row.residualN05DemandLowAmp)}..${formatMilli(row.residualN05DemandHighAmp)}; ` +
      row.classification
    );
  }
  for (const row of ts1BaseRowsForPrint) {
    console.log(
      `R14=Ts1 base window->N05: I_R14=${formatMilli(row.r14CurrentIntoN05LowAmp)}..${formatMilli(row.r14CurrentIntoN05HighAmp)}; ` +
      `N05 residual=${formatMilli(row.residualN05DemandLowAmp)}..${formatMilli(row.residualN05DemandHighAmp)}; ` +
      row.classification
    );
  }
  for (const row of n10Rows) {
    console.log(
      `N10 candidate residual active current: ${formatMilli(row.residualActiveCurrentIntoN10Amp)} into N10`
    );
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  R,
  C,
  n05DemandScenarios,
  r14EndpointTargets,
  r14BalanceRows,
  n10KclRows,
  compensationRows,
  run,
};
