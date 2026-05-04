"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
  smallSignalParams,
  hybridPiElements,
} = require("./bjt_small_signal");

const V = Object.freeze({
  V24: 24.0,
  V22: 22.0,
  V12: 12.2,
  V64: 6.4,
  N38: 3.8,
  N08: 0.8,
  N32: 3.2,
  N24: 2.4,
  N074: 0.74,
  N48: 4.8,
  N18: 1.8,
  N215: 21.5,
  N105: 10.5,
  GND: 0.0,
});

const R = Object.freeze({
  R38: 68,
  R30: 6.8e3,
  R19: 30e3,
  R12: 51e3,
  R15: 30e3,
  R18: 68e3,
  R16_MIN: 0,
  R16_MAX: 500,
  R17: 6.8e3,
  R20: 1.2e3,
  R21: 10e3,
  R22: 8.2e3,
  R25: 16e3,
  R26: 220,
  R27: 620,
  R31: 5.6e3,
  R32: 1e3,
  R36: 24,
});

const betaSweep = Object.freeze([50, 100, 200]);
const outputCurrentSweepAmp = Object.freeze([5e-3, 10e-3, 20.833333333e-3, 30e-3]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function current(fromVolt, toVolt, resistanceOhm) {
  return (fromVolt - toVolt) / resistanceOhm;
}

function micro(value) {
  return value * 1e6;
}

function milli(value) {
  return value * 1e3;
}

function passiveChecks() {
  const iR30 = current(V.V22, V.V12, R.R30);
  const iR19 = current(V.V12, V.V64, R.R19);
  const iR21 = current(V.V12, V.V64, R.R21);
  const iR25 = current(V.V12, V.N24, R.R25);
  const iR15 = current(V.V64, V.N38, R.R15);
  const iR18 = current(V.N38, V.N08, R.R18);
  const iR16R17MaxR16 = current(V.N08, V.GND, R.R16_MAX + R.R17);
  const iR16R17MinR16 = current(V.N08, V.GND, R.R16_MIN + R.R17);
  const iR20 = current(V.N08, V.N074, R.R20);
  const iR22 = current(V.N32, V.GND, R.R22);
  const iR26R27 = current(V.N074, V.GND, R.R26 + R.R27);
  const iR31 = current(V.V22, V.N48, R.R31);
  const iR32 = current(V.N18, V.GND, R.R32);
  const iR36 = current(V.V22, V.N215, R.R36);

  return {
    iR30,
    iR19,
    iR21,
    iR25,
    v12OutgoingKnownAmp: iR19 + iR21 + iR25,
    v12KclResidualAmp: iR30 - iR19 - iR21 - iR25,
    iR15,
    iR18,
    n38ActiveResidualAmp: iR15 - iR18,
    iR16R17MaxR16,
    iR16R17MinR16,
    iR20,
    n08ActiveInjectionWithoutR20MinAmp: iR16R17MaxR16 - iR18,
    n08ActiveInjectionWithoutR20MaxAmp: iR16R17MinR16 - iR18,
    n08ActiveInjectionWithR20MinAmp: iR16R17MaxR16 + iR20 - iR18,
    n08ActiveInjectionWithR20MaxAmp: iR16R17MinR16 + iR20 - iR18,
    iR22,
    iR26R27,
    iR31,
    iR32,
    iR36,
  };
}

function hybridRowsForCandidate(candidate) {
  return betaSweep.map((beta) => {
    const collectorCurrentAmp = candidate.collectorCurrentFromBeta(beta);
    const params = smallSignalParams({ currentAmp: collectorCurrentAmp, beta });
    const stamp = hybridPiElements({
      name: candidate.transistor,
      type: candidate.type,
      collector: candidate.collector,
      base: candidate.base,
      emitter: candidate.emitter,
      ...params,
    });
    return {
      transistor: candidate.transistor,
      type: candidate.type,
      terminalStatus: candidate.terminalStatus,
      collector: candidate.collector,
      base: candidate.base,
      emitter: candidate.emitter,
      beta,
      collectorCurrentAmp,
      collectorCurrentMilliAmp: milli(collectorCurrentAmp),
      baseCurrentAmp: params.baseCurrentAmp,
      baseCurrentMicroAmp: micro(params.baseCurrentAmp),
      gmSiemens: params.gmSiemens,
      gmMilliSiemens: milli(params.gmSiemens),
      emitterDynamicOhm: params.emitterDynamicOhm,
      rPiOhm: params.rPiOhm,
      roOhm: params.roOhm,
      vbeVolt: candidate.vbeVolt,
      vceVolt: candidate.vceVolt,
      currentBasis: candidate.currentBasis,
      evidence: candidate.evidence,
      limit: candidate.limit,
      hybridPiStamp: stamp,
    };
  });
}

function transistorHybridPiRows() {
  const p = passiveChecks();
  const candidates = [
    {
      transistor: "Ts2_BCY66",
      type: "npn",
      collector: "N64_T2",
      base: "N38",
      emitter: "N32",
      terminalStatus: "probable_from_VBE_and_local_KCL_not_pin_proof",
      vbeVolt: V.N38 - V.N32,
      vceVolt: V.V64 - V.N32,
      currentBasis: "IE ~= I_R22 = N32/R22 = 390 uA, if N32 is Ts2 emitter.",
      evidence: "N38=3.8 V, N32=3.2 V gives VBE=0.6 V; R22 current is coherent with a small transistor stage.",
      limit: "Collector node N64_T2 and interaction with Ts3 still require route confirmation before full nonlinear MNA.",
      collectorCurrentFromBeta: (beta) => p.iR22 * beta / (beta + 1),
    },
    {
      transistor: "Ts4_SST116_1",
      type: "npn",
      collector: "N48",
      base: "N24",
      emitter: "N18",
      terminalStatus: "probable_from_VBE_VCE_and_R32_current_not_pin_proof",
      vbeVolt: V.N24 - V.N18,
      vceVolt: V.N48 - V.N18,
      currentBasis: "IE ~= I_R32 = N18/R32 = 1.8 mA, if N18 is Ts4 emitter.",
      evidence: "N24=2.4 V, N18=1.8 V gives VBE=0.6 V and N48=4.8 V gives VCE=3.0 V.",
      limit: "Ts3/Ts4 coupling and R28/R29/C17 route still need confirmation before large-signal transistor closure.",
      collectorCurrentFromBeta: (beta) => p.iR32 * beta / (beta + 1),
    },
  ];

  return candidates.flatMap(hybridRowsForCandidate);
}

function outputStageBounds() {
  const iR36 = current(V.V22, V.N215, R.R36);
  const rows = [];
  for (const currentAmp of outputCurrentSweepAmp) {
    for (const beta of betaSweep) {
      const params = smallSignalParams({ currentAmp, beta });
      rows.push({
        transistorGroup: "Ts5_Ts6_output",
        terminalStatus: "bounded_only_no_hybrid_pi_stamp_until_topology_confirmed",
        idleCurrentAmp: currentAmp,
        idleCurrentMilliAmp: milli(currentAmp),
        beta,
        gmSiemens: params.gmSiemens,
        gmMilliSiemens: milli(params.gmSiemens),
        emitterDynamicOhm: params.emitterDynamicOhm,
        rPiOhm: params.rPiOhm,
        baseCurrentMicroAmp: micro(params.baseCurrentAmp),
        r36CurrentAmp: iR36,
        r36CurrentMilliAmp: milli(iR36),
        evidence:
          "R36 gives (22.0-21.5)/24 = 20.8 mA available into the output-stage node; N105=10.5 V is near half of N215=21.5 V.",
        limit:
          "Do not connect Ts5/Ts6 into MNA yet: collector/emitter routing, class-A/B bias, and load around C21/R34/R35/R37 are not fully proven.",
      });
    }
  }
  return rows;
}

function symbolicTransistorBoundaries(passive) {
  return [
    {
      transistorGroup: "Ts1_Ts2_front_pair",
      status: "symbolic_KCL_required_for_Ts1",
      n38ResidualMicroAmp: micro(passive.n38ActiveResidualAmp),
      n08InjectionWithR20MinMicroAmp: micro(passive.n08ActiveInjectionWithR20MinAmp),
      n08InjectionWithR20MaxMicroAmp: micro(passive.n08ActiveInjectionWithR20MaxAmp),
      reason:
        "Ts2 has a plausible VBE/current candidate, but Ts1 terminal routing is not closed. N08 needs active injection, not a passive resistor patch.",
    },
    {
      transistorGroup: "Ts3_Ts4_middle_pair",
      status: "Ts4_hybrid_pi_candidate_only",
      r31CurrentMilliAmp: milli(passive.iR31),
      r32CurrentMilliAmp: milli(passive.iR32),
      reason:
        "Ts4 has a coherent VBE/VCE candidate. Ts3 and feedback/compensation routes must stay symbolic until routing is proven.",
    },
    {
      transistorGroup: "Ts5_Ts6_output_pair",
      status: "current_bounds_only",
      r36CurrentMilliAmp: milli(passive.iR36),
      n105Volt: V.N105,
      n215Volt: V.N215,
      reason:
        "The output point is physically coherent but not enough to choose a unique small-signal topology.",
    },
  ];
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) {
    lines.push(columns.map((column) => {
      const value = row[column];
      if (typeof value === "number") return Number.isFinite(value) ? value : "";
      if (value === undefined || value === null) return "";
      return String(value).replaceAll(",", ";");
    }).join(","));
  }
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
}

function run() {
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const passive = passiveChecks();
  const hybridRows = transistorHybridPiRows();
  const outputRows = outputStageBounds();
  const boundaries = symbolicTransistorBoundaries(passive);

  const jsonPath = path.join(outDir, "b6_active_hybrid_pi_inventory.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 transistor rigorous hybrid-pi inventory",
        status:
          "B6 active transistors integrated at the only justified level: DC KCL checks, guarded hybrid-pi candidates, and output-stage bounds.",
        scientificBoundary:
          "This file does not pretend to be the final nonlinear B6 MNA. Ts1, Ts3, Ts5 and Ts6 routing remains symbolic until transistor terminals and local compensation routes are proven from the schematic/photos.",
        nodeVoltages: V,
        resistorValues: R,
        passiveChecks: passive,
        hybridPiCandidates: hybridRows,
        outputStageBounds: outputRows,
        symbolicBoundaries: boundaries,
      },
      null,
      2
    )
  );

  const passiveCsv = path.join(outDir, "b6_active_passive_kcl.csv");
  writeCsv(passiveCsv, [passive], [
    "iR30",
    "iR19",
    "iR21",
    "iR25",
    "v12OutgoingKnownAmp",
    "v12KclResidualAmp",
    "iR15",
    "iR18",
    "n38ActiveResidualAmp",
    "iR16R17MaxR16",
    "iR16R17MinR16",
    "iR20",
    "n08ActiveInjectionWithR20MinAmp",
    "n08ActiveInjectionWithR20MaxAmp",
    "iR22",
    "iR26R27",
    "iR31",
    "iR32",
    "iR36",
  ]);

  const hybridCsv = path.join(outDir, "b6_active_hybrid_pi_candidates.csv");
  writeCsv(hybridCsv, hybridRows, [
    "transistor",
    "type",
    "terminalStatus",
    "collector",
    "base",
    "emitter",
    "beta",
    "collectorCurrentMilliAmp",
    "baseCurrentMicroAmp",
    "gmMilliSiemens",
    "emitterDynamicOhm",
    "rPiOhm",
    "vbeVolt",
    "vceVolt",
    "currentBasis",
    "limit",
  ]);

  const outputCsv = path.join(outDir, "b6_output_stage_bounds.csv");
  writeCsv(outputCsv, outputRows, [
    "transistorGroup",
    "terminalStatus",
    "idleCurrentMilliAmp",
    "beta",
    "gmMilliSiemens",
    "emitterDynamicOhm",
    "rPiOhm",
    "baseCurrentMicroAmp",
    "r36CurrentMilliAmp",
    "limit",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${passiveCsv}`);
  console.log(`Wrote ${hybridCsv}`);
  console.log(`Wrote ${outputCsv}`);
  console.log("");
  console.log(`Ts2 candidate: VBE=${(V.N38 - V.N32).toFixed(3)} V, IE=${micro(passive.iR22).toFixed(1)} uA`);
  console.log(`Ts4 candidate: VBE=${(V.N24 - V.N18).toFixed(3)} V, IE=${milli(passive.iR32).toFixed(3)} mA`);
  console.log(`Output bound: R36 current=${milli(passive.iR36).toFixed(3)} mA, N105=${V.N105} V, N215=${V.N215} V`);
}

if (require.main === module) {
  run();
}

module.exports = {
  V,
  R,
  passiveChecks,
  transistorHybridPiRows,
  outputStageBounds,
  symbolicTransistorBoundaries,
  run,
};
