"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
  betaSweep,
  qPointCurrents,
} = require("./b11_active_bjt_linearization_estimates");
const {
  THERMAL_VOLTAGE_300K,
  smallSignalParams,
} = require("./bjt_small_signal");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function cleanName(value) {
  return value.replace(/[^A-Za-z0-9]+/g, "_").replace(/^_+|_+$/g, "");
}

function makeInventoryRows() {
  const rows = [];
  for (const q of qPointCurrents) {
    for (const beta of betaSweep) {
      const params = smallSignalParams({
        currentAmp: q.currentAmp,
        beta,
        thermalVoltageVolt: THERMAL_VOLTAGE_300K,
      });
      const baseName = cleanName(`${q.name}_${q.transistor}_beta${beta}`);
      rows.push({
        qPointName: q.name,
        transistor: q.transistor,
        device: q.device,
        qPointStatus: q.status,
        beta,
        currentAmp: q.currentAmp,
        gmSiemens: params.gmSiemens,
        rPiOhm: params.rPiOhm,
        rPiAdmittanceSiemens: 1 / params.rPiOhm,
        emitterDynamicOhm: params.emitterDynamicOhm,
        roOhm: params.roOhm,
        npnVccs: `${baseName}_gm: current C->E = gm*(VB-VE)`,
        pnpVccs: `${baseName}_gm: current E->C = gm*(VE-VB)`,
        rPiStamp: `${baseName}_rpi: admittance B-E = 1/rpi`,
        roStamp: "not used until Early voltage or measured ro is available",
        terminalMappingStatus: "B/C/E orientation not yet confirmed on Siemens B11 schematic",
        note: q.note,
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
  const rows = makeInventoryRows();
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b11_active_hybrid_pi_inventory.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 active hybrid-pi stamp inventory",
        status: "stamp inventory only; terminal orientation remains to be confirmed before full B11 MNA",
        equations: {
          rPiStamp: "admittance between B and E = 1/rpi",
          npnGmStamp: "VCCS current C->E = gm*(VB-VE)",
          pnpGmStamp: "VCCS current E->C = gm*(VE-VB)",
          roStamp: "optional admittance C-E = 1/ro when Early voltage or measured ro is known",
        },
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b11_active_hybrid_pi_inventory.csv");
  writeCsv(csvPath, rows, [
    "qPointName",
    "transistor",
    "device",
    "qPointStatus",
    "beta",
    "currentAmp",
    "gmSiemens",
    "rPiOhm",
    "rPiAdmittanceSiemens",
    "emitterDynamicOhm",
    "roOhm",
    "npnVccs",
    "pnpVccs",
    "rPiStamp",
    "roStamp",
    "terminalMappingStatus",
    "note",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log(`Inventory rows: ${rows.length}`);
}

if (require.main === module) {
  run();
}

module.exports = {
  makeInventoryRows,
  run,
};
