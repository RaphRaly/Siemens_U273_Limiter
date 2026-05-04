"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
  V,
  passiveCurrents,
} = require("./b11_ts1_ts2_topology_constraints");

const R = Object.freeze({
  R14: 2e3,
  R17: 6.8e3,
});

const C = Object.freeze({
  C6: 1e-9,
});

const FREQUENCIES_HZ = Object.freeze([
  10,
  20,
  100,
  1000,
  10000,
  20000,
  79577.47154594767,
  100000,
  1000000,
]);

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

function r14C6SeriesAdmittance(freqHz) {
  const omega = 2 * Math.PI * freqHz;
  const xCapOhm = 1 / (omega * C.C6);
  const denom = R.R14 * R.R14 + xCapOhm * xCapOhm;
  return {
    freqHz,
    resistanceOhm: R.R14,
    capacitiveReactanceOhm: xCapOhm,
    conductanceSiemens: R.R14 / denom,
    susceptanceSiemens: xCapOhm / denom,
    magnitudeSiemens: 1 / Math.sqrt(denom),
    phaseDegrees: Math.atan2(xCapOhm, R.R14) * 180 / Math.PI,
    currentPerVoltRmsAmp: 1 / Math.sqrt(denom),
  };
}

function dcAuditRows() {
  const p = passiveCurrents();
  const directR14Current = current(V.N10, V.N05, R.R14);
  const r17Current = current(V.N05, 0, R.R17);

  return [
    {
      caseName: "incorrect_direct_R14_N10_to_N05_DC_stress_test",
      c6Treatment: "ignored",
      r14DcCurrentAmp: directR14Current,
      n05DemandLowAmp: p.n05DemandLow,
      n05DemandHighAmp: p.n05DemandHigh,
      n05ResidualLowAmp: p.n05DemandLow - directR14Current,
      n05ResidualHighAmp: p.n05DemandHigh - directR14Current,
      verdict: "not_a_valid_dc_stamp_if_C6_is_series",
      note: "This is the step-9 stress test only; it must not be used as DC MNA if R14 reaches N10 through C6.",
    },
    {
      caseName: "correct_series_R14_C6_DC",
      c6Treatment: "open_circuit_at_dc",
      r14DcCurrentAmp: 0,
      n05DemandLowAmp: p.n05DemandLow,
      n05DemandHighAmp: p.n05DemandHigh,
      n05ResidualLowAmp: p.n05DemandLow,
      n05ResidualHighAmp: p.n05DemandHigh,
      verdict: "dc_current_must_be_supplied_by_active_network_not_R14_C6",
      note: "At DC an ideal capacitor is open; R14-C6 contributes no N10-to-N05 DC current.",
    },
    {
      caseName: "correct_series_R14_C6_DC_with_R17_candidate",
      c6Treatment: "open_circuit_at_dc",
      r14DcCurrentAmp: 0,
      n05DemandLowAmp: p.n05DemandLow + r17Current,
      n05DemandHighAmp: p.n05DemandHigh + r17Current,
      n05ResidualLowAmp: p.n05DemandLow + r17Current,
      n05ResidualHighAmp: p.n05DemandHigh + r17Current,
      verdict: "dc_current_must_be_supplied_by_active_network_not_R14_C6",
      note: "Same as above, with provisional R17=6.8k from N05 to reference.",
    },
  ];
}

function acRows() {
  return FREQUENCIES_HZ.map((freqHz) => {
    const row = r14C6SeriesAdmittance(freqHz);
    return {
      ...row,
      percentOfBareR14Conductance: 100 * row.magnitudeSiemens / (1 / R.R14),
      currentPerVoltRmsMicroAmp: micro(row.currentPerVoltRmsAmp),
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

  const dcRows = dcAuditRows();
  const ac = acRows();
  const cornerHz = 1 / (2 * Math.PI * R.R14 * C.C6);

  const jsonPath = path.join(outDir, "b11_r14_c6_dc_ac_audit.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 R14-C6 DC/AC audit",
        status: "correction audit for step 9 direct-R14 assumption",
        nodeVoltages: {
          N10: V.N10,
          N05: V.N05,
        },
        values: {
          R14: R.R14,
          C6: C.C6,
          R17: R.R17,
        },
        cornerHz,
        dcAuditRows: dcRows,
        acRows: ac,
      },
      null,
      2
    )
  );

  const dcCsv = path.join(outDir, "b11_r14_c6_dc_audit.csv");
  writeCsv(dcCsv, dcRows, [
    "caseName",
    "c6Treatment",
    "r14DcCurrentAmp",
    "n05DemandLowAmp",
    "n05DemandHighAmp",
    "n05ResidualLowAmp",
    "n05ResidualHighAmp",
    "verdict",
    "note",
  ]);

  const acCsv = path.join(outDir, "b11_r14_c6_ac_admittance.csv");
  writeCsv(acCsv, ac, [
    "freqHz",
    "resistanceOhm",
    "capacitiveReactanceOhm",
    "conductanceSiemens",
    "susceptanceSiemens",
    "magnitudeSiemens",
    "phaseDegrees",
    "currentPerVoltRmsAmp",
    "currentPerVoltRmsMicroAmp",
    "percentOfBareR14Conductance",
  ]);

  const dcCorrect = dcRows.find((row) => row.caseName === "correct_series_R14_C6_DC");
  const at20k = ac.find((row) => row.freqHz === 20000);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${dcCsv}`);
  console.log(`Wrote ${acCsv}`);
  console.log("");
  console.log(`R14-C6 corner frequency: ${cornerHz.toFixed(3)} Hz`);
  console.log(
    `Correct DC R14-C6 current: ${milli(dcCorrect.r14DcCurrentAmp).toFixed(3)} mA; ` +
    `N05 still needs ${milli(dcCorrect.n05DemandLowAmp).toFixed(3)}..${milli(dcCorrect.n05DemandHighAmp).toFixed(3)} mA`
  );
  console.log(
    `At 20 kHz, |Y_R14C6|=${at20k.magnitudeSiemens.toExponential(6)} S ` +
    `(${at20k.percentOfBareR14Conductance.toFixed(2)}% of bare R14 conductance)`
  );
}

if (require.main === module) {
  run();
}

module.exports = {
  R,
  C,
  r14C6SeriesAdmittance,
  dcAuditRows,
  acRows,
  run,
};
