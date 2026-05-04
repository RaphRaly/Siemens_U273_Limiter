"use strict";

const fs = require("node:fs");
const path = require("node:path");

const THERMAL_VOLTAGE_300K = 25.85e-3;

const betaSweep = Object.freeze([50, 100, 200]);

const qPointCurrents = Object.freeze([
  {
    name: "Ts1_from_R11",
    transistor: "B11/Ts1",
    device: "SST117",
    currentAmp: 1.15e-3,
    status: "estimate",
    note: "Current through R11 from N215=21.5 V to N10=10 V. This is a local branch current, not a closed collector current.",
  },
  {
    name: "Ts1_N05_resistive_demand_low",
    transistor: "B11/Ts1",
    device: "SST117",
    currentAmp: 4.60e-3,
    status: "bound",
    note: "Lower bound of resistive current demanded around N05=0.5 V from R12, R9, R7/R8.",
  },
  {
    name: "Ts1_N05_resistive_demand_high",
    transistor: "B11/Ts1",
    device: "SST117",
    currentAmp: 4.92e-3,
    status: "bound",
    note: "Upper bound of resistive current demanded around N05=0.5 V from R12, R9, R7/R8.",
  },
  {
    name: "Ts2_N9_main",
    transistor: "B11/Ts2",
    device: "SST117",
    currentAmp: 7.54e-3,
    status: "estimate",
    note: "R16 plus R13 current at N9=9 V with N05 about 0.5 V.",
  },
  {
    name: "Ts2_R15_supply",
    transistor: "B11/Ts2",
    device: "SST117",
    currentAmp: 7.92e-3,
    status: "estimate",
    note: "Current supplied by R15 from 24 V to N145=14.5 V.",
  },
  {
    name: "Ts3_R23_supply",
    transistor: "B11/Ts3",
    device: "SST116",
    currentAmp: 10e-3,
    status: "estimate",
    note: "Current supplied by R23 from 24 V to N23=23 V.",
  },
  {
    name: "Ts3_Ts4_local_100ohm_if_full_0p45V",
    transistor: "B11/Ts3/Ts4",
    device: "SST116",
    currentAmp: 4.5e-3,
    status: "local_check_only",
    note: "If a 100 ohm resistor really sees 0.45 V, current is 4.5 mA. This is a local check, not a proved transistor current.",
  },
  {
    name: "Ts5_Ts6_1ohm_10mV",
    transistor: "B11/Ts5/Ts6",
    device: "2N3054",
    currentAmp: 10e-3,
    status: "sweep",
    note: "Emitter resistor R28/R29 current if the 1 ohm resistor drop is 10 mV.",
  },
  {
    name: "Ts5_Ts6_1ohm_50mV",
    transistor: "B11/Ts5/Ts6",
    device: "2N3054",
    currentAmp: 50e-3,
    status: "sweep",
    note: "Emitter resistor R28/R29 current if the 1 ohm resistor drop is 50 mV.",
  },
  {
    name: "Ts5_Ts6_1ohm_100mV",
    transistor: "B11/Ts5/Ts6",
    device: "2N3054",
    currentAmp: 100e-3,
    status: "sweep",
    note: "Emitter resistor R28/R29 current if the 1 ohm resistor drop is 100 mV.",
  },
]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function bjtSmallSignal(currentAmp, beta, vt = THERMAL_VOLTAGE_300K) {
  if (!(currentAmp > 0) || !(beta > 0)) {
    throw new Error("currentAmp and beta must be positive");
  }
  const gmSiemens = currentAmp / vt;
  const emitterDynamicOhm = 1 / gmSiemens;
  const rPiOhm = beta / gmSiemens;
  const baseCurrentAmp = currentAmp / beta;
  return {
    gmSiemens,
    emitterDynamicOhm,
    rPiOhm,
    baseCurrentAmp,
  };
}

function makeRows() {
  const rows = [];
  for (const q of qPointCurrents) {
    for (const beta of betaSweep) {
      rows.push({
        ...q,
        beta,
        thermalVoltageVolt: THERMAL_VOLTAGE_300K,
        ...bjtSmallSignal(q.currentAmp, beta),
      });
    }
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
  const rows = makeRows();
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b11_active_bjt_linearization_estimates.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 active BJT first-order small-signal estimates",
        status: "parameter estimates from verified DC branch currents; not yet full active B11 MNA",
        equations: {
          thermalVoltage: "VT ~= 25.85 mV at 300 K",
          transconductance: "gm = IC / VT",
          emitterResistance: "re = 1 / gm = VT / IC",
          baseInputResistance: "rpi = beta / gm",
          baseCurrent: "IB = IC / beta",
        },
        betaSweep,
        qPointCurrents,
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b11_active_bjt_linearization_estimates.csv");
  writeCsv(csvPath, rows, [
    "name",
    "transistor",
    "device",
    "status",
    "currentAmp",
    "beta",
    "thermalVoltageVolt",
    "gmSiemens",
    "emitterDynamicOhm",
    "rPiOhm",
    "baseCurrentAmp",
    "note",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  for (const q of qPointCurrents) {
    const nominal = bjtSmallSignal(q.currentAmp, 100);
    console.log(
      `${q.name}: Ic=${(q.currentAmp * 1e3).toFixed(3)} mA, ` +
      `gm=${(nominal.gmSiemens * 1e3).toFixed(1)} mS, ` +
      `re=${nominal.emitterDynamicOhm.toFixed(2)} ohm, ` +
      `rpi(beta=100)=${nominal.rPiOhm.toFixed(0)} ohm`
    );
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  THERMAL_VOLTAGE_300K,
  betaSweep,
  qPointCurrents,
  bjtSmallSignal,
  makeRows,
  run,
};
