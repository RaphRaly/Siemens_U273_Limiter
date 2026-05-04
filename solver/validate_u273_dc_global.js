"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { runGlobalDc } = require("./u273_dc_global");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function run() {
  const { payload } = runGlobalDc();
  const failures = [];

  for (const result of payload.results) {
    if (!result.converged) failures.push(`${result.name}: did not converge`);
    if (!result.residual || result.residual.maxAbs > 1e-9) {
      failures.push(`${result.name}: residual too high`);
    }
    if (result.commandSourceOhm > 0 && result.commandSourceVolt > 0) {
      const cmd = result.nodes.CMD ?? 0;
      if (!(cmd > 0 && cmd < result.commandSourceVolt)) {
        failures.push(`${result.name}: CMD should sag below finite Thevenin drive`);
      }
    }
    if (!result.diodes.length) failures.push(`${result.name}: missing diode operating points`);
  }

  const unresolvedTargets = payload.pendingPrintedVoltageTargets.filter((x) => x.status === "DEPENDS-ON-P0");
  if (unresolvedTargets.length !== payload.pendingPrintedVoltageTargets.length) {
    failures.push("Printed Siemens targets must remain DEPENDS-ON-P0 in the Thevenin reference stage");
  }

  const validation = {
    title: "Validation u273_dc_global",
    status: failures.length ? "FAIL" : "PASS_WITH_PARAMETRIC_BOUNDARIES",
    boundary: payload.scientificBoundary,
    checks: {
      scenarioCount: payload.results.length,
      allConverged: payload.results.every((x) => x.converged),
      allPrintedTargetsDeferred: unresolvedTargets.length === payload.pendingPrintedVoltageTargets.length,
    },
    failures,
  };

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "u273_dc_global_validation.json");
  fs.writeFileSync(filePath, JSON.stringify(validation, null, 2));

  if (failures.length) {
    throw new Error(`u273_dc_global validation failed: ${failures.join("; ")}`);
  }

  console.log("u273_dc_global validation passed with parametric boundaries");
  console.log(`Wrote ${filePath}`);
}

if (require.main === module) {
  run();
}

module.exports = { run };
