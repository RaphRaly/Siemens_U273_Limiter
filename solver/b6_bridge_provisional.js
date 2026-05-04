"use strict";

const { solveDc, solutionObject } = require("./mna_core");

const OHM = 1;
const K = 1e3;

const diodeModels = {
  // Placeholder values. These are deliberately parameters, not claims about the real parts.
  SSD55: { is: 1e-12, n: 1.7, vt: 25.852e-3 },
  OA154Q: { is: 1e-8, n: 1.6, vt: 25.852e-3 },
};

function buildProvisionalBridgeNetlist(vcmd, options = {}) {
  const r7 = options.r7 ?? 100 * OHM;
  const r8 = options.r8 ?? 250 * K;
  const r10 = options.r10 ?? 20 * K;
  const r6 = options.r6 ?? 39 * K;
  const r11 = options.r11 ?? 39 * K;
  const r9 = options.r9 ?? 390 * K;

  return {
    title: "Siemens U273 B6 bridge provisional DC netlist",
    notes: [
      "This is topology variant A, not a final schematic extraction.",
      "Capacitors are open for DC.",
      "VCMD is imposed as an external B11 command source.",
      "Diode parameters are placeholders and must be calibrated.",
    ],
    voltageSources: [
      { name: "VCMD", nPlus: "CMD", nMinus: "0", value: vcmd },
    ],
    resistors: [
      { name: "R10", n1: "CMD", n2: "NB", value: r10 },
      { name: "R9", n1: "NB", n2: "0", value: r9 },
      { name: "R7", n1: "NL", n2: "NR", value: r7 },
      { name: "R8", n1: "N14", n2: "N15", value: r8 },
      { name: "R6", n1: "N14", n2: "0", value: r6 },
      { name: "R11", n1: "N15", n2: "0", value: r11 },
      // Very large leakage path keeps floating internal nodes numerically referenced
      // without materially changing conduction in normal command ranges.
      { name: "GMIN_NL", n1: "NL", n2: "0", value: 1e12 },
      { name: "GMIN_NR", n1: "NR", n2: "0", value: 1e12 },
    ],
    diodes: [
      { name: "D3_SSD55", anode: "NB", cathode: "NL", ...diodeModels.SSD55 },
      { name: "D4_OA154Q", anode: "NL", cathode: "N14", ...diodeModels.OA154Q },
      { name: "D2_SSD55", anode: "NB", cathode: "NR", ...diodeModels.SSD55 },
      { name: "D1_OA154Q", anode: "NR", cathode: "N15", ...diodeModels.OA154Q },
    ],
  };
}

function formatVolts(value) {
  return `${value.toFixed(6)} V`;
}

function formatAmps(value) {
  const abs = Math.abs(value);
  if (abs >= 1) return `${value.toFixed(6)} A`;
  if (abs >= 1e-3) return `${(value * 1e3).toFixed(3)} mA`;
  if (abs >= 1e-6) return `${(value * 1e6).toFixed(3)} uA`;
  if (abs >= 1e-9) return `${(value * 1e9).toFixed(3)} nA`;
  return `${value.toExponential(3)} A`;
}

function formatOhms(value) {
  if (!Number.isFinite(value)) return "inf";
  if (value >= 1e6) return `${(value / 1e6).toFixed(3)} MOhm`;
  if (value >= 1e3) return `${(value / 1e3).toFixed(3)} kOhm`;
  return `${value.toFixed(3)} Ohm`;
}

function runSweep() {
  const commands = [0, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0];
  let previous = null;

  console.log("Siemens U273 B6 bridge provisional DC MNA sweep");
  console.log("Topology: variant A, for solver validation only");
  console.log("C1/C2/C3/C5/C7 are open in this DC solve");
  console.log("");

  for (const vcmd of commands) {
    const netlist = buildProvisionalBridgeNetlist(vcmd);
    const result = solveDc(netlist, {
      initialGuess: previous,
      damping: 0.6,
      tolerance: 1e-10,
      maxIterations: 100,
    });
    const sol = solutionObject(result, netlist);
    previous = result.solution;

    console.log(`VCMD = ${formatVolts(vcmd)} | converged=${result.converged} | iterations=${result.iterations}`);
    console.log(`  NB=${formatVolts(sol.NB || 0)} NL=${formatVolts(sol.NL || 0)} NR=${formatVolts(sol.NR || 0)} N14=${formatVolts(sol.N14 || 0)} N15=${formatVolts(sol.N15 || 0)}`);
    console.log(`  source current I(VCMD)=${formatAmps(sol["I(VCMD)"] || 0)}`);
    for (const d of result.diodeInfo) {
      console.log(`  ${d.name}: vd=${formatVolts(d.vd)} id=${formatAmps(d.id)} rd=${formatOhms(d.rd)}`);
    }
    console.log("");
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  buildProvisionalBridgeNetlist,
};
