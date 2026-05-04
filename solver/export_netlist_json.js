"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { buildNetlist } = require("./u273_orchestrator");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function exportJson(options = {}) {
  const netlist = buildNetlist(options);
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "u273_netlist.json");
  fs.writeFileSync(filePath, JSON.stringify(netlist, null, 2));
  return { filePath, netlist };
}

function run() {
  const { filePath, netlist } = exportJson();
  console.log(`Wrote ${filePath}`);
  console.log(`${netlist.components.length} components, ${netlist.nodes.length} non-ground nodes`);
}

if (require.main === module) {
  run();
}

module.exports = {
  exportJson,
  run,
};
