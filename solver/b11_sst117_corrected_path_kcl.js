"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { smallSignalParams } = require("./bjt_small_signal");

const V = Object.freeze({
  N215: 21.5,
  N145: 14.5,
  N10: 10,
  N9: 9,
  N05: 0.5,
  REF: 0,
});

const R = Object.freeze({
  R11: 10e3,
  R12: 120,
  R13: 220e3,
  R15: 1.2e3,
  R16: 1.2e3,
  R17: 6.8e3,
  R9: 56e3,
  R7R8_MIN: 680,
  R7R8_MAX: 1180,
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

function passiveCurrents() {
  const iR15 = current(V.N215, V.N145, R.R15);
  const iR11 = current(V.N215, V.N10, R.R11);
  const iR16IfBottomRef = current(V.N9, V.REF, R.R16);
  const iR13 = current(V.N9, V.N05, R.R13);
  const iR12 = current(V.N05, V.REF, R.R12);
  const iR9 = current(V.N05, V.REF, R.R9);
  const iR17 = current(V.N05, V.REF, R.R17);
  const iR7R8Low = current(V.N05, V.REF, R.R7R8_MAX);
  const iR7R8High = current(V.N05, V.REF, R.R7R8_MIN);
  const n05DemandLow = iR12 + iR9 + iR7R8Low;
  const n05DemandHigh = iR12 + iR9 + iR7R8High;

  return {
    iR15,
    iR11,
    iR16IfBottomRef,
    iR13,
    iR12,
    iR9,
    iR17,
    iR7R8Low,
    iR7R8High,
    n9PassiveDemandIfR16BottomRefAmp: iR16IfBottomRef + iR13,
    n9ShortfallIfR16BottomRefAndTs2CollectorOnlyAmp: iR16IfBottomRef + iR13 - iR15,
    n05DemandLowAmp: n05DemandLow,
    n05DemandHighAmp: n05DemandHigh,
    n05DemandLowWithR17Amp: n05DemandLow + iR17,
    n05DemandHighWithR17Amp: n05DemandHigh + iR17,
    n05ActiveDeficitAfterR13LowAmp: n05DemandLow - iR13,
    n05ActiveDeficitAfterR13HighAmp: n05DemandHigh - iR13,
    n05ActiveDeficitAfterR13LowWithR17Amp: n05DemandLow + iR17 - iR13,
    n05ActiveDeficitAfterR13HighWithR17Amp: n05DemandHigh + iR17 - iR13,
  };
}

function ts2ConsistencyRows() {
  const p = passiveCurrents();
  return betaSweep.map((beta) => {
    const expectedBaseCurrent = p.iR15 / beta;
    const expectedEmitterCurrent = p.iR15 + expectedBaseCurrent;
    const requiredBaseCurrentIfR16BottomRef = p.n9PassiveDemandIfR16BottomRefAmp - p.iR15;
    const effectiveBetaIfR16BottomRef = requiredBaseCurrentIfR16BottomRef > 0
      ? p.iR15 / requiredBaseCurrentIfR16BottomRef
      : Infinity;
    const requiredR16CurrentForThisBeta = expectedEmitterCurrent - p.iR13;
    const requiredR16BottomVoltForThisBeta = V.N9 - requiredR16CurrentForThisBeta * R.R16;
    const params = smallSignalParams({ currentAmp: p.iR15, beta });
    return {
      beta,
      assumedCollectorCurrentAmp: p.iR15,
      expectedBaseCurrentAmp: expectedBaseCurrent,
      expectedEmitterCurrentAmp: expectedEmitterCurrent,
      r13CurrentAmp: p.iR13,
      requiredR16CurrentForThisBetaAmp: requiredR16CurrentForThisBeta,
      requiredR16BottomVoltForThisBeta,
      r16CurrentIfBottomRefAmp: p.iR16IfBottomRef,
      requiredEmitterCurrentAtN9IfR16BottomRefAmp: p.n9PassiveDemandIfR16BottomRefAmp,
      requiredBaseOrExtraInjectionIfR16BottomRefAmp: requiredBaseCurrentIfR16BottomRef,
      effectiveBetaNeededIfR16BottomRef: effectiveBetaIfR16BottomRef,
      gmSiemens: params.gmSiemens,
      reOhm: params.emitterDynamicOhm,
      rPiOhm: params.rPiOhm,
      verdict: requiredR16BottomVoltForThisBeta > 1.5 && requiredR16BottomVoltForThisBeta < 2.5
        ? "coherent_if_R16_bottom_is_about_2V"
        : "requires_topology_review",
      note: "R15=N215->N145 is visually confirmed. R16 lower node is not assumed; the row computes the lower-node voltage required by BJT KCL.",
    };
  });
}

function pathRows() {
  const p = passiveCurrents();
  return [
    {
      path: "N215_R15_N145_Ts2_N9_R13_N05",
      confirmedPassivePart: "R15 feeds N145; R13 weakly links N9 to N05; C6/R14 is DC-open.",
      r15CurrentAmp: p.iR15,
      r13CurrentAmp: p.iR13,
      n05DemandLowAmp: p.n05DemandLowAmp,
      n05DemandHighAmp: p.n05DemandHighAmp,
      n05CoveredByR13LowFraction: p.iR13 / p.n05DemandLowAmp,
      n05CoveredByR13HighFraction: p.iR13 / p.n05DemandHighAmp,
      conclusion: "R13 covers less than 1 percent of N05 DC demand; the strong N05 closure must be through Ts1 or another active branch, not through a passive Ts2/R13 path.",
    },
    {
      path: "N215_R11_N10_Ts1_N05",
      confirmedPassivePart: "R11 feeds N10; Ts1 terminal assignment still must be stamped before final BJT MNA.",
      r11CurrentAmp: p.iR11,
      n05DemandLowAmp: p.n05DemandLowAmp,
      n05DemandHighAmp: p.n05DemandHighAmp,
      n05DeficitIfR11AloneLowAmp: p.n05DemandLowAmp - p.iR11,
      n05DeficitIfR11AloneHighAmp: p.n05DemandHighAmp - p.iR11,
      conclusion: "R11 alone is not enough to supply the N05 load; Ts1 cannot be reduced to a simple R11-fed isolated emitter/collector current without additional active coupling.",
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

  const passive = passiveCurrents();
  const ts2Rows = ts2ConsistencyRows();
  const paths = pathRows();

  const jsonPath = path.join(outDir, "b11_sst117_corrected_path_kcl.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 SST117 Ts1/Ts2 corrected KCL from PDF crop",
        status: "node-level DC path proof; transistor terminal stamps still guarded",
        imageEvidence: {
          transistors: "Ts1=SST117, Ts2=SST117",
          visibleNodes: "N215=21.5 V, N145=14.5 V, N10=10 V, N9=9 V, N05=0.5 V",
          visibleBranches: "R15=1.2k, R11=10k, R16=1.2k, R13=220k, R14=2k in series with C6=1n",
        },
      nodeVoltages: V,
      resistorValues: R,
      passiveCurrents: passive,
        ts2ConsistencyRows: ts2Rows,
        pathRows: paths,
      },
      null,
      2
    )
  );

  const passiveCsv = path.join(outDir, "b11_sst117_corrected_passive_kcl.csv");
  writeCsv(passiveCsv, [passive], [
    "iR15",
    "iR11",
    "iR16IfBottomRef",
    "iR13",
    "iR12",
    "iR9",
    "iR17",
    "iR7R8Low",
    "iR7R8High",
    "n9PassiveDemandIfR16BottomRefAmp",
    "n9ShortfallIfR16BottomRefAndTs2CollectorOnlyAmp",
    "n05DemandLowAmp",
    "n05DemandHighAmp",
    "n05DemandLowWithR17Amp",
    "n05DemandHighWithR17Amp",
    "n05ActiveDeficitAfterR13LowAmp",
    "n05ActiveDeficitAfterR13HighAmp",
  ]);

  const ts2Csv = path.join(outDir, "b11_sst117_ts2_consistency.csv");
  writeCsv(ts2Csv, ts2Rows, [
    "beta",
    "assumedCollectorCurrentAmp",
    "expectedBaseCurrentAmp",
    "expectedEmitterCurrentAmp",
    "r13CurrentAmp",
    "requiredR16CurrentForThisBetaAmp",
    "requiredR16BottomVoltForThisBeta",
    "r16CurrentIfBottomRefAmp",
    "requiredEmitterCurrentAtN9IfR16BottomRefAmp",
    "requiredBaseOrExtraInjectionIfR16BottomRefAmp",
    "effectiveBetaNeededIfR16BottomRef",
    "gmSiemens",
    "reOhm",
    "rPiOhm",
    "verdict",
    "note",
  ]);

  const pathCsv = path.join(outDir, "b11_sst117_corrected_path_rows.csv");
  writeCsv(pathCsv, paths, [
    "path",
    "confirmedPassivePart",
    "r15CurrentAmp",
    "r13CurrentAmp",
    "r11CurrentAmp",
    "n05DemandLowAmp",
    "n05DemandHighAmp",
    "n05CoveredByR13LowFraction",
    "n05CoveredByR13HighFraction",
    "n05DeficitIfR11AloneLowAmp",
    "n05DeficitIfR11AloneHighAmp",
    "conclusion",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${passiveCsv}`);
  console.log(`Wrote ${ts2Csv}`);
  console.log(`Wrote ${pathCsv}`);
  console.log("");
  console.log(`R15 N215->N145: ${milli(passive.iR15).toFixed(3)} mA`);
  console.log(`R11 N215->N10: ${milli(passive.iR11).toFixed(3)} mA`);
  console.log(`R16 N9->ref candidate only: ${milli(passive.iR16IfBottomRef).toFixed(3)} mA`);
  console.log(`R13 N9->N05: ${micro(passive.iR13).toFixed(3)} uA`);
  console.log(`N9 passive demand if R16 bottom=0: ${milli(passive.n9PassiveDemandIfR16BottomRefAmp).toFixed(3)} mA`);
  console.log(`N9 shortfall if R16 bottom=0: ${milli(passive.n9ShortfallIfR16BottomRefAndTs2CollectorOnlyAmp).toFixed(3)} mA`);
  console.log(`N05 demand without R17: ${milli(passive.n05DemandLowAmp).toFixed(3)}..${milli(passive.n05DemandHighAmp).toFixed(3)} mA`);
  console.log(`N05 active deficit after R13: ${milli(passive.n05ActiveDeficitAfterR13LowAmp).toFixed(3)}..${milli(passive.n05ActiveDeficitAfterR13HighAmp).toFixed(3)} mA`);
  for (const row of ts2Rows) {
    console.log(
      `beta=${row.beta}: expected IE=${milli(row.expectedEmitterCurrentAmp).toFixed(3)} mA, ` +
      `R16 bottom required=${row.requiredR16BottomVoltForThisBeta.toFixed(3)} V, ` +
      `beta_eff_if_R16_bottom_0=${row.effectiveBetaNeededIfR16BottomRef.toFixed(2)}`
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
  ts2ConsistencyRows,
  pathRows,
  run,
};
