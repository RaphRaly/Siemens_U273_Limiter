"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { solveDc } = require("./mna_core");
const { buildProvisionalBridgeNetlist } = require("./b6_bridge_provisional");
const { c, abs, solveAc } = require("./complex_ac");

const TWO_PI = 2 * Math.PI;

const componentValues = {
  r5: 5.6e3,
  r6: 39e3,
  r7: 100,
  r8: 250e3,
  r9: 390e3,
  r10: 20e3,
  r11: 39e3,
  c1: 1000e-12,
  c2: 22e-6,
  c3: 4.7e-6,
  c5: 150e-6,
  c7: 4.7e-6,
  gmin: 1e-12,
};

function yRes(value) {
  return c(1 / value, 0);
}

function yCap(frequency, value) {
  return c(0, TWO_PI * frequency * value);
}

function db20(value) {
  if (value <= 0) return -Infinity;
  return 20 * Math.log10(value);
}

function phaseDeg(value) {
  return Math.atan2(value.im, value.re) * 180 / Math.PI;
}

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function diodeConductanceMap(diodeInfo) {
  return Object.fromEntries(diodeInfo.map((d) => [d.name, d.gd]));
}

function solveOperatingPoint(vcmd, initialGuess) {
  const dcNetlist = buildProvisionalBridgeNetlist(vcmd);
  const result = solveDc(dcNetlist, {
    initialGuess,
    damping: 0.6,
    tolerance: 1e-10,
    maxIterations: 100,
  });
  if (!result.converged) {
    throw new Error(`DC operating point did not converge for VCMD=${vcmd}`);
  }
  return result;
}

function buildAcNetlist(frequency, diodeGd) {
  const v = componentValues;
  return {
    title: "Siemens U273 B6 bridge provisional small-signal AC netlist",
    admittances: [
      { name: "R5", n1: "VS", n2: "NX", y: yRes(v.r5) },
      { name: "C1_1000p", n1: "NX", n2: "NA", y: yCap(frequency, v.c1) },
      { name: "C2", n1: "NA", n2: "NB", y: yCap(frequency, v.c2) },
      { name: "R10_to_ac_ground", n1: "NB", n2: "0", y: yRes(v.r10) },
      { name: "R9", n1: "NB", n2: "0", y: yRes(v.r9) },
      { name: "R7", n1: "NL", n2: "NR", y: yRes(v.r7) },
      { name: "R8", n1: "N14", n2: "N15", y: yRes(v.r8) },
      { name: "R6", n1: "N14", n2: "0", y: yRes(v.r6) },
      { name: "R11", n1: "N15", n2: "0", y: yRes(v.r11) },
      { name: "D3_gd", n1: "NB", n2: "NL", y: c(diodeGd.D3_SSD55, 0) },
      { name: "D4_gd", n1: "NL", n2: "N14", y: c(diodeGd.D4_OA154Q, 0) },
      { name: "D2_gd", n1: "NB", n2: "NR", y: c(diodeGd.D2_SSD55, 0) },
      { name: "D1_gd", n1: "NR", n2: "N15", y: c(diodeGd.D1_OA154Q, 0) },
      { name: "C3", n1: "N14", n2: "0", y: yCap(frequency, v.c3) },
      { name: "C5", n1: "N14", n2: "N15", y: yCap(frequency, v.c5) },
      { name: "C7", n1: "N15", n2: "0", y: yCap(frequency, v.c7) },
      { name: "GMIN_NL", n1: "NL", n2: "0", y: c(v.gmin, 0) },
      { name: "GMIN_NR", n1: "NR", n2: "0", y: c(v.gmin, 0) },
    ],
    voltageSources: [
      { name: "VAC", nPlus: "VS", nMinus: "0", value: c(1, 0) },
    ],
  };
}

function nodeMagnitude(solution, node) {
  return abs(solution[node] || c(0, 0));
}

function serializeComplex(value) {
  return { re: value.re, im: value.im, mag: abs(value), phaseDeg: phaseDeg(value) };
}

function runSweep() {
  const commands = [0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0];
  const frequencies = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
  const rows = [];
  const operatingPoints = [];
  let previousDc = null;

  for (const vcmd of commands) {
    const dcResult = solveOperatingPoint(vcmd, previousDc);
    previousDc = dcResult.solution;
    const gd = diodeConductanceMap(dcResult.diodeInfo);
    operatingPoints.push({
      vcmd,
      iterations: dcResult.iterations,
      diodeConductances: gd,
    });

    for (const frequency of frequencies) {
      const acNetlist = buildAcNetlist(frequency, gd);
      const ac = solveAc(acNetlist);
      const s = ac.solution;
      const vna = s.NA || c(0, 0);
      const vnb = s.NB || c(0, 0);
      const vnx = s.NX || c(0, 0);
      const vacCurrent = s["I(VAC)"] || c(0, 0);

      rows.push({
        vcmd,
        frequency,
        VNA: serializeComplex(vna),
        VNB: serializeComplex(vnb),
        VNX: serializeComplex(vnx),
        IVAC: serializeComplex(vacCurrent),
        inputCurrentMagnitude: abs(vacCurrent),
        inputImpedanceMagnitude: abs(vacCurrent) > 0 ? 1 / abs(vacCurrent) : Infinity,
        gainDbAtNA: db20(nodeMagnitude(s, "NA")),
      });
    }
  }

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b6_bridge_variantA_ac_sweep.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 provisional bridge small-signal AC sweep",
        topology: "variant A; command source held at AC ground; not final schematic extraction",
        warning: "Results are provisional. C1=1000 pF in series with the audio branch is now confirmed; final audio conclusions still require Zcmd(s)/B11 loading, C4/C6 adjustment values, wiper settings, diode calibration, and the exact downstream load.",
        componentValues,
        operatingPoints,
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b6_bridge_variantA_ac_sweep.csv");
  const header = [
    "vcmd",
    "frequency_hz",
    "VNA_mag",
    "VNA_db",
    "VNA_phase_deg",
    "VNB_mag",
    "VNB_db",
    "VNX_mag",
    "VNX_db",
    "input_current_mag",
    "input_impedance_mag",
  ];
  const lines = [header.join(",")];
  for (const row of rows) {
    lines.push(
      [
        row.vcmd,
        row.frequency,
        row.VNA.mag,
        db20(row.VNA.mag),
        row.VNA.phaseDeg,
        row.VNB.mag,
        db20(row.VNB.mag),
        row.VNX.mag,
        db20(row.VNX.mag),
        row.inputCurrentMagnitude,
        row.inputImpedanceMagnitude,
      ].join(",")
    );
  }
  fs.writeFileSync(csvPath, `${lines.join("\n")}\n`);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  for (const vcmd of commands) {
    const at1k = rows.find((row) => row.vcmd === vcmd && row.frequency === 1000);
    const at20k = rows.find((row) => row.vcmd === vcmd && row.frequency === 20000);
    console.log(
      `VCMD=${vcmd.toFixed(1)} V: |VNA|@1k=${at1k.VNA.mag.toExponential(6)} (${db20(at1k.VNA.mag).toFixed(2)} dB), ` +
      `|VNA|@20k=${at20k.VNA.mag.toExponential(6)} (${db20(at20k.VNA.mag).toFixed(2)} dB)`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  buildAcNetlist,
  runSweep,
};
