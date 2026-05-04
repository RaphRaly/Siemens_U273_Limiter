"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { buildNetlist } = require("./u273_orchestrator");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function cell(value) {
  if (value === undefined || value === null) return "";
  if (typeof value === "number") return Number.isFinite(value) ? String(value) : "";
  if (typeof value === "object") return JSON.stringify(value).replaceAll("|", "/");
  return String(value).replaceAll("|", "/");
}

function componentNodeText(nodes) {
  if (!nodes) return "";
  return Object.entries(nodes)
    .map(([name, node]) => `${name}=${node}`)
    .join(";");
}

function renderText(netlist) {
  const lines = [];
  lines.push("Siemens U273 - netlist parametrique export texte");
  lines.push("====================================================");
  lines.push("");
  lines.push(`version: ${netlist.version}`);
  lines.push(`status: ${netlist.status}`);
  lines.push(`coupling_mode: ${netlist.coupling_mode}`);
  lines.push(`boundary: ${netlist.scientific_boundary}`);
  lines.push("");
  lines.push("Components");
  lines.push("id | type | value | unit | nodes | card | function | status | source_etape");

  for (const item of netlist.components) {
    lines.push([
      item.id,
      item.type,
      item.value,
      item.unit,
      componentNodeText(item.nodes),
      item.card,
      item.function,
      item.status,
      item.source_etape,
    ].map(cell).join(" | "));
  }

  lines.push("");
  lines.push("Switches");
  lines.push(`S6 status: ${netlist.switches.S6.status}`);
  lines.push(`S6 selected_position: ${netlist.switches.S6.selected_position}`);
  lines.push(`S6 delivery_linear: ${netlist.switches.S6.delivery_linear_reading.contacts.join("; ")}`);
  lines.push(`S7 status: ${netlist.switches.S7.status}`);
  lines.push(`S7 selected_mode: ${netlist.switches.S7.selected_mode}`);
  for (const entry of netlist.switches.S7.contact_table) {
    lines.push(`S7 ${entry.position}: closed=${entry.closed}, open=${entry.open}, zener=${entry.zenerPath}, mode=${entry.inferredMode}`);
  }

  lines.push("");
  lines.push("Open scientific boundaries");
  lines.push("- B11 active regulator is not yet a direct MNA block.");
  lines.push("- B6 bridge diode law is closed, but final polarities remain photo-dependent.");
  lines.push("- B6 Ts1/Ts3/Ts5/Ts6 and B11 Ts1/Ts2 stay symbolic or unconfirmed until route proof.");
  lines.push("- S6 non-delivery positions and S1..S5 are not final contact tables.");
  lines.push("");

  return `${lines.join("\n")}\n`;
}

function exportTxt(options = {}) {
  const netlist = buildNetlist(options);
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "u273_netlist.txt");
  fs.writeFileSync(filePath, renderText(netlist));
  return { filePath, netlist };
}

function run() {
  const { filePath, netlist } = exportTxt();
  console.log(`Wrote ${filePath}`);
  console.log(`${netlist.components.length} components, ${netlist.nodes.length} non-ground nodes`);
}

if (require.main === module) {
  run();
}

module.exports = {
  renderText,
  exportTxt,
  run,
};
