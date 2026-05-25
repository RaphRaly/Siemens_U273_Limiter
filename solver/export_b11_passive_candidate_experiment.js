"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { buildB11PassiveCandidateExperiment } = require("./u273_orchestrator");

const ENABLE_FLAG = "--enable-b11-passive-candidate-experiment";

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function exportExperiment(args = process.argv.slice(2)) {
  if (!args.includes(ENABLE_FLAG)) {
    throw new Error(
      `B11 passive candidate experiment is opt-in only. Re-run with ${ENABLE_FLAG}.`
    );
  }

  const report = buildB11PassiveCandidateExperiment({
    enableB11PassiveCandidateExperiment: true,
  });

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "b11_passive_candidate_experiment.json");
  fs.writeFileSync(filePath, JSON.stringify(report, null, 2));
  return { filePath, report };
}

function run() {
  const { filePath, report } = exportExperiment();
  console.log(`Wrote ${filePath}`);
  console.log(
    `${report.result.stamped_resistor_count} stamped passive resistors, ` +
    `${report.result.excluded_component_count} excluded guarded components`
  );
  console.log(
    `${report.result.active_current_requirement_summary.constraint_count} non-rail current constraints, ` +
    `largest at ${report.result.active_current_requirement_summary.largest_constraint_node}`
  );
  console.log(`isolated DC converged: ${report.result.isolated_dc_solve.converged}`);
}

if (require.main === module) {
  run();
}

module.exports = {
  ENABLE_FLAG,
  exportExperiment,
  run,
};
