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
  buildS6Admittances,
} = require("./b11_s6_zout_sweep");

const loadHypotheses = Object.freeze([
  {
    name: "r1r2_open",
    note: "No R1/R2 load applied at VR.",
    rload: Infinity,
  },
  {
    name: "r1_3k",
    note: "R1=3 kOhm applied as a shunt load at VR.",
    rload: 3e3,
  },
  {
    name: "r2_1M",
    note: "R2=1 MOhm adjustable applied as a shunt load at VR.",
    rload: 1e6,
  },
  {
    name: "r1_r2_series_1003k",
    note: "R1+R2=1.003 MOhm applied as a shunt load at VR.",
    rload: 1.003e6,
  },
  {
    name: "r1_r2_parallel_2k991",
    note: "R1||R2=2.991 kOhm applied as a shunt load at VR.",
    rload: 1 / (1 / 3e3 + 1 / 1e6),
  },
]);

function add(a, b) {
  return c(a.re + b.re, a.im + b.im);
}

function neg(a) {
  return c(-a.re, -a.im);
}

function div(a, b) {
  const d = b.re * b.re + b.im * b.im;
  return c((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}

function yRes(value) {
  return c(1 / value, 0);
}

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

function buildLoadedZoutNetlist(frequency, contactHypothesis, loadHypothesis) {
  const admittances = buildS6Admittances(frequency, contactHypothesis);
  if (Number.isFinite(loadHypothesis.rload)) {
    admittances.push({
      name: `LOAD_${loadHypothesis.name}`,
      n1: "VR",
      n2: "0",
      y: yRes(loadHypothesis.rload),
    });
  }
  return {
    title: "Siemens U273 B11 S6 contact/load bounded Zout test",
    contactHypothesis: contactHypothesis.name,
    loadHypothesis: loadHypothesis.name,
    admittances,
    voltageSources: [
      { name: "VIN_SHORT", nPlus: "VL", nMinus: "0", value: c(0, 0) },
      { name: "VTEST_OUT", nPlus: "VR", nMinus: "0", value: c(1, 0) },
    ],
  };
}

function solveLoadedZout(frequency, contactHypothesis, loadHypothesis) {
  const netlist = buildLoadedZoutNetlist(frequency, contactHypothesis, loadHypothesis);
  const result = solveAc(netlist);
  const sourceCurrent = result.solution["I(VTEST_OUT)"] || c(0, 0);
  const networkCurrent = neg(sourceCurrent);
  const zout = abs(networkCurrent) > 0 ? div(c(1, 0), networkCurrent) : c(Infinity, 0);
  return {
    frequency,
    contactHypothesis: contactHypothesis.name,
    loadHypothesis: loadHypothesis.name,
    loadOhm: loadHypothesis.rload,
    networkCurrent,
    sourceCurrent,
    zout,
  };
}

function solveOperatingPoints(commands, options = {}) {
  const out = new Map();
  let previous = null;
  for (const vcmd of commands) {
    const netlist = buildB6ParametricDcNetlist(vcmd, options);
    const result = solveDc(netlist, {
      initialGuess: previous,
      damping: 0.6,
      tolerance: 1e-10,
      maxIterations: 120,
    });
    if (!result.converged) {
      throw new Error(`B6 DC operating point did not converge for VCMD=${vcmd}`);
    }
    previous = result.solution;
    out.set(vcmd, diodeConductanceMap(result.diodeInfo));
  }
  return out;
}

function runSweep() {
  const frequencies = [100, 1000, 10000, 20000];
  const commands = [0, 3];
  const baseB6Options = {
    r7EffectiveOhm: 100,
    r8EffectiveOhm: 250e3,
    rAmpInputOhm: Infinity,
    commandAcMode: "open",
    zcmdOhm: Infinity,
    c4AbglFarad: 0,
    c6AbglFarad: 0,
  };

  const operatingPoints = solveOperatingPoints(commands, baseB6Options);
  const rows = [];

  for (const contactHypothesis of contactHypotheses) {
    for (const loadHypothesis of loadHypotheses) {
      for (const frequency of frequencies) {
        const zoutResult = solveLoadedZout(frequency, contactHypothesis, loadHypothesis);
        for (const vcmd of commands) {
          const diodeGd = operatingPoints.get(vcmd);
          const acNetlist = buildB6ParametricAcNetlist(frequency, diodeGd, baseB6Options);
          acNetlist.admittances.push({
            name: `YS6_BOUND_${contactHypothesis.name}_${loadHypothesis.name}`,
            n1: "CMD",
            n2: "0",
            y: c(zoutResult.networkCurrent.re, zoutResult.networkCurrent.im),
          });
          const ac = solveAc(acNetlist);
          const s = ac.solution;
          const vna = s.NA || c(0, 0);
          const vcmdNode = s.CMD || c(0, 0);
          rows.push({
            contactHypothesis: contactHypothesis.name,
            contactNote: contactHypothesis.note,
            loadHypothesis: loadHypothesis.name,
            loadNote: loadHypothesis.note,
            loadOhm: loadHypothesis.rload,
            frequency,
            vcmd,
            s6Zout: serializeComplex(zoutResult.zout),
            s6Yout: serializeComplex(zoutResult.networkCurrent),
            VNA: serializeComplex(vna),
            VCMD_NODE: serializeComplex(vcmdNode),
          });
        }
      }
    }
  }

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b11_s6_contact_bound_sweep.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 S6 contact/load bound sweep",
        status: "bounded sensitivity because exact S6/S7 contacts are not readable in available crops",
        warning: "R1/R2 are applied as possible simple shunt loads at VR. R2 is corrected to the 1 MOhm adjustable value visible in the global crop. This is a bound, not a confirmed contact table.",
        contactHypotheses,
        loadHypotheses,
        frequencies,
        commands,
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b11_s6_contact_bound_sweep.csv");
  const header = [
    "contact_hypothesis",
    "load_hypothesis",
    "load_ohm",
    "frequency_hz",
    "vcmd",
    "s6_zout_mag_ohm",
    "s6_zout_phase_deg",
    "VNA_mag",
    "VNA_db",
    "VCMD_NODE_mag",
  ];
  const lines = [header.join(",")];
  for (const row of rows) {
    lines.push([
      row.contactHypothesis,
      row.loadHypothesis,
      row.loadOhm,
      row.frequency,
      row.vcmd,
      row.s6Zout.mag,
      row.s6Zout.phaseDeg,
      row.VNA.mag,
      db20(row.VNA.mag),
      row.VCMD_NODE.mag,
    ].join(","));
  }
  fs.writeFileSync(csvPath, `${lines.join("\n")}\n`);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  for (const loadHypothesis of loadHypotheses) {
    const candidates = rows.filter((row) => row.loadHypothesis === loadHypothesis.name && row.frequency === 1000 && row.vcmd === 0);
    const dbs = candidates.map((row) => db20(row.VNA.mag));
    const zouts = candidates.map((row) => row.s6Zout.mag);
    console.log(
      `${loadHypothesis.name}: Zout@1k range=${Math.min(...zouts).toFixed(1)}..${Math.max(...zouts).toFixed(1)} Ohm, ` +
      `B6 VCMD=0 VNA@1k range=${Math.min(...dbs).toFixed(2)}..${Math.max(...dbs).toFixed(2)} dB`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  loadHypotheses,
  buildLoadedZoutNetlist,
  solveLoadedZout,
  runSweep,
};
