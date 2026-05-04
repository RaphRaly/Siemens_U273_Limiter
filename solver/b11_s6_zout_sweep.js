"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { c, abs, solveAc } = require("./complex_ac");

const TWO_PI = 2 * Math.PI;

const values = Object.freeze({
  r3: 7.5e3,
  r4: 7.5e3,
  r5: 15e3,
  r6: 3.9e3,
  c1: 5e-9,
  c2: 5e-9,
  c3: 3.3e-9,
  c4: 3e-9,
});

const contactHypotheses = Object.freeze([
  {
    name: "core_all_branches",
    note: "Current provisional S6 core: LM, MR, LR and VM-to-ground RC branch active.",
    branches: { lm: true, mr: true, lr: true, m0: true },
  },
  {
    name: "core_without_lr_bridge",
    note: "Sensitivity case: LR bridge R5/C3 open.",
    branches: { lm: true, mr: true, lr: false, m0: true },
  },
  {
    name: "core_without_m0_shunt",
    note: "Sensitivity case: VM-to-ground R6/C4 branch open.",
    branches: { lm: true, mr: true, lr: true, m0: false },
  },
  {
    name: "core_series_lm_mr_only",
    note: "Sensitivity case: only LM and MR paths active.",
    branches: { lm: true, mr: true, lr: false, m0: false },
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

function yCap(frequency, value) {
  return c(0, TWO_PI * frequency * value);
}

function ySeriesRC(frequency, r, cap) {
  const zc = c(0, -1 / (TWO_PI * frequency * cap));
  return div(c(1, 0), add(c(r, 0), zc));
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

function buildS6Admittances(frequency, hypothesis) {
  const v = values;
  const h = hypothesis.branches;
  const admittances = [];

  if (h.lm) {
    admittances.push({ name: "R4_LM", n1: "VL", n2: "VM", y: yRes(v.r4) });
    admittances.push({ name: "C2_LM", n1: "VL", n2: "VM", y: yCap(frequency, v.c2) });
  }
  if (h.mr) {
    admittances.push({ name: "R3_MR", n1: "VM", n2: "VR", y: yRes(v.r3) });
    admittances.push({ name: "C1_MR", n1: "VM", n2: "VR", y: yCap(frequency, v.c1) });
  }
  if (h.lr) {
    admittances.push({ name: "R5_LR", n1: "VL", n2: "VR", y: yRes(v.r5) });
    admittances.push({ name: "C3_LR", n1: "VL", n2: "VR", y: yCap(frequency, v.c3) });
  }
  if (h.m0) {
    admittances.push({ name: "R6_C4_series_M0", n1: "VM", n2: "0", y: ySeriesRC(frequency, v.r6, v.c4) });
  }

  return admittances;
}

function buildS6ZoutNetlist(frequency, hypothesis) {
  return {
    title: "Siemens U273 B11 S6 core output impedance test",
    hypothesis: hypothesis.name,
    admittances: buildS6Admittances(frequency, hypothesis),
    voltageSources: [
      { name: "VIN_SHORT", nPlus: "VL", nMinus: "0", value: c(0, 0) },
      { name: "VTEST_OUT", nPlus: "VR", nMinus: "0", value: c(1, 0) },
    ],
  };
}

function solveZout(frequency, hypothesis) {
  const netlist = buildS6ZoutNetlist(frequency, hypothesis);
  const result = solveAc(netlist);
  const sourceCurrent = result.solution["I(VTEST_OUT)"] || c(0, 0);
  const networkCurrent = neg(sourceCurrent);
  const zout = abs(networkCurrent) > 0 ? div(c(1, 0), networkCurrent) : c(Infinity, 0);
  return {
    frequency,
    hypothesis: hypothesis.name,
    note: hypothesis.note,
    sourceCurrent: serializeComplex(sourceCurrent),
    networkCurrent: serializeComplex(networkCurrent),
    zout: serializeComplex(zout),
  };
}

function runSweep() {
  const frequencies = [20, 50, 100, 200, 500, 1000, 2000, 3200, 4200, 5000, 10000, 13600, 20000];
  const rows = [];

  for (const hypothesis of contactHypotheses) {
    for (const frequency of frequencies) {
      rows.push(solveZout(frequency, hypothesis));
    }
  }

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b11_s6_zout_sweep.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 S6 provisional output impedance sweep",
        status: "S6-core impedance extraction; not full B11 Zcmd(s)",
        warning: "This is the passive S6 core only. Full Zcmd(s) also depends on active B11 stages, S6/S7 contacts, detector state, and downstream coupling.",
        values,
        contactHypotheses,
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b11_s6_zout_sweep.csv");
  const header = [
    "hypothesis",
    "frequency_hz",
    "zout_mag_ohm",
    "zout_phase_deg",
    "zout_re_ohm",
    "zout_im_ohm",
    "network_current_mag_a",
  ];
  const lines = [header.join(",")];
  for (const row of rows) {
    lines.push([
      row.hypothesis,
      row.frequency,
      row.zout.mag,
      row.zout.phaseDeg,
      row.zout.re,
      row.zout.im,
      row.networkCurrent.mag,
    ].join(","));
  }
  fs.writeFileSync(csvPath, `${lines.join("\n")}\n`);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  for (const hypothesis of contactHypotheses) {
    const at1k = rows.find((row) => row.hypothesis === hypothesis.name && row.frequency === 1000);
    const at20k = rows.find((row) => row.hypothesis === hypothesis.name && row.frequency === 20000);
    console.log(
      `${hypothesis.name}: Zout@1k=${at1k.zout.mag.toFixed(1)} Ohm (${at1k.zout.phaseDeg.toFixed(1)} deg), ` +
      `Zout@20k=${at20k.zout.mag.toFixed(1)} Ohm (${at20k.zout.phaseDeg.toFixed(1)} deg)`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  values,
  contactHypotheses,
  buildS6Admittances,
  buildS6ZoutNetlist,
  solveZout,
  runSweep,
};
