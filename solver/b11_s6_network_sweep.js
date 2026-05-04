"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { c, abs, solveAc } = require("./complex_ac");

const TWO_PI = 2 * Math.PI;

const values = {
  r3: 7.5e3,
  r4: 7.5e3,
  r5: 15e3,
  r6: 3.9e3,
  c1: 5e-9,
  c2: 5e-9,
  c3: 3.3e-9,
  c4: 3e-9,
};

function yRes(value) {
  return c(1 / value, 0);
}

function yCap(frequency, value) {
  return c(0, TWO_PI * frequency * value);
}

function ySeriesRC(frequency, r, cap) {
  const x = 1 / (TWO_PI * frequency * cap);
  const den = r * r + x * x;
  return c(r / den, x / den);
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

function buildS6CoreNetlist(frequency, rload) {
  const v = values;
  const admittances = [
    { name: "R4_LM", n1: "VL", n2: "VM", y: yRes(v.r4) },
    { name: "C2_LM", n1: "VL", n2: "VM", y: yCap(frequency, v.c2) },
    { name: "R3_MR", n1: "VM", n2: "VR", y: yRes(v.r3) },
    { name: "C1_MR", n1: "VM", n2: "VR", y: yCap(frequency, v.c1) },
    { name: "R5_LR", n1: "VL", n2: "VR", y: yRes(v.r5) },
    { name: "C3_LR", n1: "VL", n2: "VR", y: yCap(frequency, v.c3) },
    { name: "R6_C4_series_M0", n1: "VM", n2: "0", y: ySeriesRC(frequency, v.r6, v.c4) },
  ];

  if (Number.isFinite(rload)) {
    admittances.push({ name: "RLOAD", n1: "VR", n2: "0", y: yRes(rload) });
  }

  return {
    title: "Siemens U273 B11 S6 core AC network, provisional",
    admittances,
    voltageSources: [
      { name: "VIN", nPlus: "VL", nMinus: "0", value: c(1, 0) },
    ],
  };
}

function serializeComplex(value) {
  return { re: value.re, im: value.im, mag: abs(value), phaseDeg: phaseDeg(value) };
}

function runSweep() {
  const frequencies = [20, 50, 100, 200, 500, 1000, 2000, 3200, 4200, 5000, 10000, 13600, 20000];
  const loads = [1e6, 100e3, 10e3];
  const rows = [];

  for (const rload of loads) {
    for (const frequency of frequencies) {
      const netlist = buildS6CoreNetlist(frequency, rload);
      const result = solveAc(netlist);
      const vr = result.solution.VR || c(0, 0);
      const vm = result.solution.VM || c(0, 0);
      rows.push({
        rload,
        frequency,
        VM: serializeComplex(vm),
        VR: serializeComplex(vr),
        gainDb: db20(abs(vr)),
      });
    }
  }

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b11_s6_core_ac_sweep.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 S6 core AC sweep",
        topology: "Provisional linear core from MNA B11 document; exact S6 contacts not finalized.",
        warning: "Do not interpret this as the final B11 frequency law until S6 contact states and loading are confirmed.",
        values,
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b11_s6_core_ac_sweep.csv");
  const header = ["rload_ohm", "frequency_hz", "VR_mag", "VR_db", "VR_phase_deg", "VM_mag", "VM_db"];
  const lines = [header.join(",")];
  for (const row of rows) {
    lines.push(
      [
        row.rload,
        row.frequency,
        row.VR.mag,
        row.gainDb,
        row.VR.phaseDeg,
        row.VM.mag,
        db20(row.VM.mag),
      ].join(",")
    );
  }
  fs.writeFileSync(csvPath, `${lines.join("\n")}\n`);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  for (const rload of loads) {
    const at1k = rows.find((row) => row.rload === rload && row.frequency === 1000);
    const at10k = rows.find((row) => row.rload === rload && row.frequency === 10000);
    const at20k = rows.find((row) => row.rload === rload && row.frequency === 20000);
    console.log(
      `Rload=${rload} Ohm: VR@1k=${db20(at1k.VR.mag).toFixed(2)} dB, ` +
      `VR@10k=${db20(at10k.VR.mag).toFixed(2)} dB, VR@20k=${db20(at20k.VR.mag).toFixed(2)} dB`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  buildS6CoreNetlist,
  runSweep,
};
