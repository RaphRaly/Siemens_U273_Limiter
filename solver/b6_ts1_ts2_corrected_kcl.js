"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { smallSignalParams } = require("./bjt_small_signal");

const V = Object.freeze({
  V12: 12.2,
  V64: 6.4,
  N38: 3.8,
  N32: 3.2,
  N08: 0.8,
  N074: 0.74,
  GND: 0,
});

const R = Object.freeze({
  R15: 30e3,
  R18: 68e3,
  R16_MIN: 0,
  R16_MAX: 500,
  R17: 6.8e3,
  R20: 1.2e3,
  R21: 10e3,
  R22: 8.2e3,
});

const betaSweep = Object.freeze([50, 100, 200]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function current(fromVolt, toVolt, resistanceOhm) {
  return (fromVolt - toVolt) / resistanceOhm;
}

function micro(value) {
  return value * 1e6;
}

function passiveCurrents() {
  const iR15 = current(V.V64, V.N38, R.R15);
  const iR18 = current(V.N38, V.N08, R.R18);
  const iR16R17MaxR16 = current(V.N08, V.GND, R.R16_MAX + R.R17);
  const iR16R17MinR16 = current(V.N08, V.GND, R.R16_MIN + R.R17);
  const iR20 = current(V.N08, V.N074, R.R20);
  const iR21 = current(V.V12, V.V64, R.R21);
  const iR22 = current(V.N32, V.GND, R.R22);

  return {
    iR15,
    iR18,
    iR16R17MaxR16,
    iR16R17MinR16,
    iR20,
    iR21,
    iR22,
    n38ActiveResidualAmp: iR15 - iR18,
    n08ActiveInjectionWithoutR20MinAmp: iR16R17MaxR16 - iR18,
    n08ActiveInjectionWithoutR20MaxAmp: iR16R17MinR16 - iR18,
    n08ActiveInjectionWithR20MinAmp: iR16R17MaxR16 + iR20 - iR18,
    n08ActiveInjectionWithR20MaxAmp: iR16R17MinR16 + iR20 - iR18,
  };
}

function ts2EmitterRows() {
  const p = passiveCurrents();
  return betaSweep.map((beta) => {
    const emitterCurrentAmp = p.iR22;
    const baseCurrentAmp = emitterCurrentAmp / (beta + 1);
    const collectorCurrentAmp = emitterCurrentAmp - baseCurrentAmp;
    const params = smallSignalParams({ currentAmp: collectorCurrentAmp, beta });
    return {
      beta,
      assumedEmitterCurrentAmp: emitterCurrentAmp,
      collectorCurrentAmp,
      baseCurrentAmp,
      r21SupplyAmp: p.iR21,
      residualR21ToTs3OrOtherAmp: p.iR21 - collectorCurrentAmp,
      gmSiemens: params.gmSiemens,
      reOhm: params.emitterDynamicOhm,
      rPiOhm: params.rPiOhm,
      note: "Only valid if N38 is Ts2 base and N32 is Ts2 emitter; this is a consistency check, not a pin proof.",
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

  const passive = passiveCurrents();
  const ts2Rows = ts2EmitterRows();

  const jsonPath = path.join(outDir, "b6_ts1_ts2_corrected_kcl.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 Ts1/Ts2 corrected KCL after PDF read",
        status: "corrected resistor values; active terminal assignment still guarded",
        corrections: {
          R17: "6.8 kOhm, not 68 kOhm",
          R20: "1.2 kOhm, not 12 kOhm",
        },
        nodeVoltages: V,
        resistorValues: R,
        passiveCurrents: passive,
        ts2EmitterConsistencyRows: ts2Rows,
      },
      null,
      2
    )
  );

  const passiveCsv = path.join(outDir, "b6_ts1_ts2_corrected_passive_kcl.csv");
  writeCsv(passiveCsv, [passive], [
    "iR15",
    "iR18",
    "iR16R17MaxR16",
    "iR16R17MinR16",
    "iR20",
    "iR21",
    "iR22",
    "n38ActiveResidualAmp",
    "n08ActiveInjectionWithoutR20MinAmp",
    "n08ActiveInjectionWithoutR20MaxAmp",
    "n08ActiveInjectionWithR20MinAmp",
    "n08ActiveInjectionWithR20MaxAmp",
  ]);

  const ts2Csv = path.join(outDir, "b6_ts2_corrected_small_signal.csv");
  writeCsv(ts2Csv, ts2Rows, [
    "beta",
    "assumedEmitterCurrentAmp",
    "collectorCurrentAmp",
    "baseCurrentAmp",
    "r21SupplyAmp",
    "residualR21ToTs3OrOtherAmp",
    "gmSiemens",
    "reOhm",
    "rPiOhm",
    "note",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${passiveCsv}`);
  console.log(`Wrote ${ts2Csv}`);
  console.log("");
  console.log(`R15 6.4->3.8 current: ${micro(passive.iR15).toFixed(2)} uA`);
  console.log(`R18 3.8->0.8 current: ${micro(passive.iR18).toFixed(2)} uA`);
  console.log(`N38 active residual: ${micro(passive.n38ActiveResidualAmp).toFixed(2)} uA`);
  console.log(
    `R16+R17 0.8->0 current: ` +
    `${micro(passive.iR16R17MaxR16).toFixed(2)}..${micro(passive.iR16R17MinR16).toFixed(2)} uA`
  );
  console.log(`R20 0.8->0.74 current: ${micro(passive.iR20).toFixed(2)} uA`);
  console.log(
    `N08 active injection required with R20: ` +
    `${micro(passive.n08ActiveInjectionWithR20MinAmp).toFixed(2)}..` +
    `${micro(passive.n08ActiveInjectionWithR20MaxAmp).toFixed(2)} uA`
  );
  for (const row of ts2Rows) {
    console.log(
      `beta=${row.beta}: Ts2 Ic=${micro(row.collectorCurrentAmp).toFixed(2)} uA, ` +
      `Ib=${micro(row.baseCurrentAmp).toFixed(2)} uA, ` +
      `gm=${(row.gmSiemens * 1e3).toFixed(2)} mS`
    );
  }
}

if (require.main === module) {
  run();
}

module.exports = {
  V,
  R,
  passiveCurrents,
  ts2EmitterRows,
  run,
};
