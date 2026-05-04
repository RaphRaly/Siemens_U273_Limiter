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
    const dcResult = solveDc(dcNetlist, {
      initialGuess: previous,
      damping: 0.6,
      tolerance: 1e-10,
      maxIterations: 120,
    });
    if (!dcResult.converged) {
      throw new Error(`B6 parametric DC operating point did not converge for VCMD=${vcmd}`);
    }
    previous = dcResult.solution;
    out.set(vcmd, {
      iterations: dcResult.iterations,
      diodeConductances: diodeConductanceMap(dcResult.diodeInfo),
      diodeInfo: dcResult.diodeInfo,
    });
  }
  return out;
}

function scenarioRows(scenario, operatingPoints, commands, frequencies) {
  const rows = [];
  for (const vcmd of commands) {
    const op = operatingPoints.get(vcmd);
    for (const frequency of frequencies) {
      const acNetlist = buildB6ParametricAcNetlist(frequency, op.diodeConductances, scenario.options);
      const ac = solveAc(acNetlist);
      const s = ac.solution;
      const vna = s.NA || c(0, 0);
      const vnb = s.NB || c(0, 0);
      const vcmdNode = s.CMD || c(0, 0);
      const vacCurrent = s["I(VAC)"] || c(0, 0);
      rows.push({
        scenario: scenario.name,
        note: scenario.note,
        vcmd,
        frequency,
        commandModel: acNetlist.commandModel,
        c4AbglFarad: acNetlist.parameters.c4Abgl,
        c6AbglFarad: acNetlist.parameters.c6Abgl,
        r7EffectiveOhm: acNetlist.parameters.r7,
        r8EffectiveOhm: acNetlist.parameters.r8,
        zcmdOhm: acNetlist.parameters.zcmd,
        VNA: serializeComplex(vna),
        VNB: serializeComplex(vnb),
        VCMD_NODE: serializeComplex(vcmdNode),
        IVAC: serializeComplex(vacCurrent),
        inputCurrentMagnitude: abs(vacCurrent),
        inputImpedanceMagnitude: abs(vacCurrent) > 0 ? 1 / abs(vacCurrent) : Infinity,
      });
    }
  }
  return rows;
}

function runSweep() {
  const commands = [0, 1, 2, 3];
  const frequencies = [100, 1000, 10000, 20000];
  const baseOptions = {
    r7EffectiveOhm: 100,
    r8EffectiveOhm: 250e3,
    rAmpInputOhm: Infinity,
  };
  const scenarios = [
    {
      name: "cmd_ideal_c4c6_0",
      note: "Old local test limit: command node held at AC ground, C4/C6 omitted.",
      options: { ...baseOptions, commandAcMode: "ideal", zcmdOhm: 0, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
    {
      name: "cmd_10k_c4c6_0",
      note: "Finite B11 output impedance sensitivity, C4/C6 omitted.",
      options: { ...baseOptions, zcmdOhm: 10e3, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
    {
      name: "cmd_100k_c4c6_0",
      note: "Finite B11 output impedance sensitivity, C4/C6 omitted.",
      options: { ...baseOptions, zcmdOhm: 100e3, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
    {
      name: "cmd_1M_c4c6_0",
      note: "Finite B11 output impedance sensitivity, C4/C6 omitted.",
      options: { ...baseOptions, zcmdOhm: 1e6, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
    {
      name: "cmd_open_c4c6_0",
      note: "Upper sensitivity bound: command port left open in AC.",
      options: { ...baseOptions, commandAcMode: "open", zcmdOhm: Infinity, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
    {
      name: "cmd_100k_c4c6_100p",
      note: "Adjustment-capacitor sensitivity with symmetrical 100 pF C4/C6.",
      options: { ...baseOptions, zcmdOhm: 100e3, c4AbglFarad: 100e-12, c6AbglFarad: 100e-12 },
    },
    {
      name: "cmd_100k_c4c6_1n",
      note: "Adjustment-capacitor sensitivity with symmetrical 1 nF C4/C6.",
      options: { ...baseOptions, zcmdOhm: 100e3, c4AbglFarad: 1e-9, c6AbglFarad: 1e-9 },
    },
  ];

  const operatingPoints = solveOperatingPoints(commands, baseOptions);
  const rows = scenarios.flatMap((scenario) => scenarioRows(scenario, operatingPoints, commands, frequencies));

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b6_parametric_ac_sensitivity.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 parametric AC sensitivity",
        status: "scientific sensitivity study, not final full-device curve",
        warning: "C1=1000 pF is fixed. C4/C6, B11 output impedance Zcmd(s), R7/R8 settings, diode calibration, and downstream load remain parameters.",
        commands,
        frequencies,
        operatingPoints: Object.fromEntries(operatingPoints),
        scenarios,
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b6_parametric_ac_sensitivity.csv");
  const header = [
    "scenario",
    "vcmd",
    "frequency_hz",
    "command_model",
    "zcmd_ohm",
    "c4_f",
    "c6_f",
    "VNA_mag",
    "VNA_db",
    "VNB_mag",
    "VNB_db",
    "VCMD_NODE_mag",
    "VCMD_NODE_db",
    "input_impedance_mag",
  ];
  const lines = [header.join(",")];
  for (const row of rows) {
    lines.push([
      row.scenario,
      row.vcmd,
      row.frequency,
      row.commandModel,
      row.zcmdOhm,
      row.c4AbglFarad,
      row.c6AbglFarad,
      row.VNA.mag,
      db20(row.VNA.mag),
      row.VNB.mag,
      db20(row.VNB.mag),
      row.VCMD_NODE.mag,
      db20(row.VCMD_NODE.mag),
      row.inputImpedanceMagnitude,
    ].join(","));
  }
  fs.writeFileSync(csvPath, `${lines.join("\n")}\n`);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  for (const scenario of scenarios) {
    const row1k = rows.find((row) => row.scenario === scenario.name && row.vcmd === 3 && row.frequency === 1000);
    const row20k = rows.find((row) => row.scenario === scenario.name && row.vcmd === 3 && row.frequency === 20000);
    console.log(
      `${scenario.name}: VCMD=3V |VNA|@1k=${row1k.VNA.mag.toExponential(6)} (${db20(row1k.VNA.mag).toFixed(2)} dB), ` +
      `|VNA|@20k=${row20k.VNA.mag.toExponential(6)} (${db20(row20k.VNA.mag).toFixed(2)} dB)`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  runSweep,
  solveOperatingPoints,
  scenarioRows,
};
