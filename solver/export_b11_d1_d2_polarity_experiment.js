"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
  ENABLE_FLAG,
  buildB11D1D2PolarityExperiment,
} = require("./b11_d1_d2_polarity_experiment");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function exportExperiment(args = process.argv.slice(2)) {
  if (!args.includes(ENABLE_FLAG)) {
    throw new Error(
      `B11 D1/D2 polarity experiment is opt-in only. Re-run with ${ENABLE_FLAG}.`
    );
  }

  const report = buildB11D1D2PolarityExperiment({
    enableB11D1D2PolarityExperiment: true,
  });

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "b11_d1_d2_polarity_experiment.json");
  fs.writeFileSync(filePath, JSON.stringify(report, null, 2));
  return { filePath, report };
}

function run() {
  const { filePath, report } = exportExperiment();
  console.log(`Wrote ${filePath}`);
  console.log(
    `${report.result.candidate_count} candidate polarity pairs, ` +
    `${report.result.functional_pass_candidate_count} functional pass, ` +
    `${report.result.rejected_candidate_count} rejected controls`
  );
  console.log(`future guarded stamp candidate: ${report.result.accepted_for_future_guarded_mna_stamp}`);
  console.log(
    `B11_NCMD_LOCAL separate from B6 CMD: ` +
    `${report.result.closure_verdict.b11_ncmd_local_separate_from_b6_cmd}`
  );
}

if (require.main === module) {
  run();
}

module.exports = {
  exportExperiment,
  run,
};
