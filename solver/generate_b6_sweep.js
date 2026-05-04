"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { solveDc, solutionObject } = require("./mna_core");
const { buildProvisionalBridgeNetlist } = require("./b6_bridge_provisional");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function finiteOrNull(value) {
  return Number.isFinite(value) ? value : null;
}

function runSweep() {
  const commands = [0, 0.1, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0];
  const rows = [];
  let previous = null;

  for (const vcmd of commands) {
    const netlist = buildProvisionalBridgeNetlist(vcmd);
    const result = solveDc(netlist, {
      initialGuess: previous,
      damping: 0.6,
      tolerance: 1e-10,
      maxIterations: 100,
    });
    previous = result.solution;
    const sol = solutionObject(result, netlist);

    const row = {
      vcmd,
      converged: result.converged,
      iterations: result.iterations,
      NB: sol.NB ?? 0,
      NL: sol.NL ?? 0,
      NR: sol.NR ?? 0,
      N14: sol.N14 ?? 0,
      N15: sol.N15 ?? 0,
      sourceCurrent: sol["I(VCMD)"] ?? 0,
      diodes: result.diodeInfo.map((d) => ({
        name: d.name,
        vd: d.vd,
        id: d.id,
        gd: d.gd,
        rd: finiteOrNull(d.rd),
      })),
    };
    rows.push(row);
  }

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b6_bridge_variantA_dc_sweep.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 provisional bridge DC sweep",
        topology: "variant A, solver validation only",
        warning: "Diode parameters and topology are provisional; do not treat as final U273 data.",
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b6_bridge_variantA_dc_sweep.csv");
  const header = [
    "vcmd",
    "converged",
    "iterations",
    "NB",
    "NL",
    "NR",
    "N14",
    "N15",
    "sourceCurrent",
    "D3_id",
    "D3_rd",
    "D4_id",
    "D4_rd",
    "D2_id",
    "D2_rd",
    "D1_id",
    "D1_rd",
  ];
  const lines = [header.join(",")];
  for (const row of rows) {
    const byName = Object.fromEntries(row.diodes.map((d) => [d.name, d]));
    lines.push(
      [
        row.vcmd,
        row.converged,
        row.iterations,
        row.NB,
        row.NL,
        row.NR,
        row.N14,
        row.N15,
        row.sourceCurrent,
        byName.D3_SSD55.id,
        byName.D3_SSD55.rd,
        byName.D4_OA154Q.id,
        byName.D4_OA154Q.rd,
        byName.D2_SSD55.id,
        byName.D2_SSD55.rd,
        byName.D1_OA154Q.id,
        byName.D1_OA154Q.rd,
      ].join(",")
    );
  }
  fs.writeFileSync(csvPath, `${lines.join("\n")}\n`);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
}

if (require.main === module) {
  runSweep();
}
