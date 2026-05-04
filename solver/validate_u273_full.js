"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { runGlobalDc } = require("./u273_dc_global");
const { runGlobalAc } = require("./u273_ac_global");
const { runTransient } = require("./u273_transient");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function run() {
  const dc = runGlobalDc();
  const ac = runGlobalAc();
  const transient = runTransient();
  const failures = [];

  if (!dc.payload.results.every((row) => row.converged)) {
    failures.push("DC: at least one scenario did not converge");
  }
  if (!dc.payload.results.every((row) => row.residual && row.residual.maxAbs <= 1e-9)) {
    failures.push("DC: at least one scenario has excessive residual");
  }
  if (!ac.payload.rows.length) failures.push("AC: no rows generated");
  if (!ac.payload.rows.some((row) => row.frequency === 1000 && row.driveVolt === 3)) {
    failures.push("AC: missing 1 kHz / 3 V reference rows");
  }
  if (!transient.payload.rows.length) failures.push("Transient: no rows generated");
  if (!transient.payload.summaries.every((row) => row.maxCmdVolt >= 0)) {
    failures.push("Transient: invalid command voltage summary");
  }

  const report = {
    title: "Siemens U273 full validation report",
    status: failures.length ? "FAIL" : "PASS_WITH_GUARDED_BOUNDARIES",
    boundary:
      "PASS means the rigorous current chain runs: DC Thevenin reference, AC bridge/command small-signal, and bounded quasi-static transient. It does not mean full active BJT closure.",
    generated: {
      dc: [dc.jsonPath, dc.csvPath],
      ac: [ac.jsonPath, ac.csvPath],
      transient: [transient.jsonPath, transient.csvPath],
    },
    summary: {
      dcScenarios: dc.payload.results.length,
      acRows: ac.payload.rows.length,
      transientRows: transient.payload.rows.length,
      transientCases: transient.payload.summaries.length,
    },
    failures,
  };

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "u273_validation_report.json");
  fs.writeFileSync(filePath, JSON.stringify(report, null, 2));

  if (failures.length) {
    throw new Error(`Full validation failed: ${failures.join("; ")}`);
  }

  console.log("u273 full validation passed with guarded boundaries");
  console.log(`Wrote ${filePath}`);
}

if (require.main === module) {
  run();
}

module.exports = { run };
