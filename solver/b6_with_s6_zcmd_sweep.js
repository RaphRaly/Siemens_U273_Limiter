"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { solveDc } = require("./mna_core");
const { c, abs, solveAc } = require("./complex_ac");
const {
  buildB6ParametricDcNetlist,
  buildB6ParametricAcNetlist,
  diodeConductanceMap,
} = require("./b6_parametric");
const {
  contactHypotheses,
  solveZout,
} = require("./b11_s6_zout_sweep");

function db20(value) {
  if (value <= 0) return -Infinity;
  return 20 * Math.log10(value);
}

function phaseDeg(value) {
  return Math.atan2(value.im, value.re) * 180 / Math.PI;
}

function serializeComplex(value) {
  return { re: value.re, im: value.im, mag: abs(value), phaseDeg: phaseDeg(value) };
}

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function solveOperatingPoints(commands, options = {}) {
  const out = new Map();
  let previous = null;
  for (const vcmd of commands) {
    const dcNetlist = buildB6ParametricDcNetlist(vcmd, options);
    const result = solveDc(dcNetlist, {
      initialGuess: previous,
      damping: 0.6,
      tolerance: 1e-10,
      maxIterations: 120,
    });
    if (!result.converged) {
      throw new Error(`B6 DC operating point did not converge for VCMD=${vcmd}`);
    }
    previous = result.solution;
    out.set(vcmd, {
      iterations: result.iterations,
      diodeConductances: diodeConductanceMap(result.diodeInfo),
      diodeInfo: result.diodeInfo,
    });
  }
  return out;
}

function runSweep() {
  const commands = [0, 1, 2, 3];
  const frequencies = [100, 1000, 10000, 20000];
  const baseOptions = {
    r7EffectiveOhm: 100,
    r8EffectiveOhm: 250e3,
    rAmpInputOhm: Infinity,
    commandAcMode: "open",
    zcmdOhm: Infinity,
    c4AbglFarad: 0,
    c6AbglFarad: 0,
  };

  const operatingPoints = solveOperatingPoints(commands, baseOptions);
  const rows = [];

  for (const hypothesis of contactHypotheses) {
    for (const vcmd of commands) {
      const op = operatingPoints.get(vcmd);
      for (const frequency of frequencies) {
        const s6 = solveZout(frequency, hypothesis);
        const ycmd = c(s6.networkCurrent.re, s6.networkCurrent.im);
        const acNetlist = buildB6ParametricAcNetlist(frequency, op.diodeConductances, baseOptions);
        acNetlist.admittances.push({ name: `YS6_${hypothesis.name}`, n1: "CMD", n2: "0", y: ycmd });
        acNetlist.commandModel = `S6 passive-core Zout hypothesis ${hypothesis.name}`;
        const result = solveAc(acNetlist);
        const sol = result.solution;
        const vna = sol.NA || c(0, 0);
        const vnb = sol.NB || c(0, 0);
        const vcmdNode = sol.CMD || c(0, 0);
        const vacCurrent = sol["I(VAC)"] || c(0, 0);
        rows.push({
          hypothesis: hypothesis.name,
          note: hypothesis.note,
          vcmd,
          frequency,
          s6Zout: s6.zout,
          s6Yout: serializeComplex(ycmd),
          VNA: serializeComplex(vna),
          VNB: serializeComplex(vnb),
          VCMD_NODE: serializeComplex(vcmdNode),
          IVAC: serializeComplex(vacCurrent),
          inputImpedanceMagnitude: abs(vacCurrent) > 0 ? 1 / abs(vacCurrent) : Infinity,
        });
      }
    }
  }

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b6_with_s6_zcmd_sweep.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 driven by provisional passive S6 output impedance",
        status: "coupled sensitivity study, not full B11 model",
        warning: "Only passive S6-core output impedance is connected to B6 CMD. Active B11 stages, S6/S7 contact truth, detector state and diode/transistor calibration are still absent.",
        commands,
        frequencies,
        contactHypotheses,
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b6_with_s6_zcmd_sweep.csv");
  const header = [
    "hypothesis",
    "vcmd",
    "frequency_hz",
    "s6_zout_mag_ohm",
    "s6_zout_phase_deg",
    "VNA_mag",
    "VNA_db",
    "VCMD_NODE_mag",
    "input_impedance_mag",
  ];
  const lines = [header.join(",")];
  for (const row of rows) {
    lines.push([
      row.hypothesis,
      row.vcmd,
      row.frequency,
      row.s6Zout.mag,
      row.s6Zout.phaseDeg,
      row.VNA.mag,
      db20(row.VNA.mag),
      row.VCMD_NODE.mag,
      row.inputImpedanceMagnitude,
    ].join(","));
  }
  fs.writeFileSync(csvPath, `${lines.join("\n")}\n`);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  for (const hypothesis of contactHypotheses) {
    const at0_1k = rows.find((row) => row.hypothesis === hypothesis.name && row.vcmd === 0 && row.frequency === 1000);
    const at3_1k = rows.find((row) => row.hypothesis === hypothesis.name && row.vcmd === 3 && row.frequency === 1000);
    const at0_20k = rows.find((row) => row.hypothesis === hypothesis.name && row.vcmd === 0 && row.frequency === 20000);
    const at3_20k = rows.find((row) => row.hypothesis === hypothesis.name && row.vcmd === 3 && row.frequency === 20000);
    console.log(
      `${hypothesis.name}: ` +
      `VCMD=0 |VNA| 1k=${db20(at0_1k.VNA.mag).toFixed(2)} dB, 20k=${db20(at0_20k.VNA.mag).toFixed(2)} dB; ` +
      `VCMD=3 |VNA| 1k=${db20(at3_1k.VNA.mag).toFixed(2)} dB, 20k=${db20(at3_20k.VNA.mag).toFixed(2)} dB`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  runSweep,
};
