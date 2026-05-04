"use strict";

const { solveDc, solutionObject } = require("./mna_core");
const { diodeCurrentMicroAmpFromVoltageMilliVolt } = require("./u273_diode_empirical");

function assertClose(name, actual, expected, relTol) {
  const rel = Math.abs((actual - expected) / expected);
  if (rel > relTol) {
    throw new Error(`${name}: expected ${expected}, got ${actual}, rel=${rel}`);
  }
}

function run() {
  const netlist = {
    title: "Validation: two U273 empirical diodes in series under 1 V",
    voltageSources: [
      { name: "VTEST", nPlus: "VIN", nMinus: "0", value: 1.0 },
    ],
    diodes: [
      { name: "D_TOP", anode: "VIN", cathode: "MID", model: "u273_empirical" },
      { name: "D_BOTTOM", anode: "MID", cathode: "0", model: "u273_empirical" },
    ],
    resistors: [
      { name: "GMIN_MID", n1: "MID", n2: "0", value: 1e12 },
    ],
  };

  const result = solveDc(netlist, {
    damping: 0.35,
    tolerance: 1e-11,
    maxIterations: 240,
  });
  if (!result.converged) {
    throw new Error("Empirical two-diode MNA validation did not converge");
  }

  const sol = solutionObject(result, netlist);
  const expectedCurrentMicroAmp = diodeCurrentMicroAmpFromVoltageMilliVolt(500);
  const top = result.diodeInfo.find((d) => d.name === "D_TOP");
  const bottom = result.diodeInfo.find((d) => d.name === "D_BOTTOM");

  assertClose("MID voltage", sol.MID, 0.5, 2e-3);
  assertClose("D_TOP current", top.id * 1e6, expectedCurrentMicroAmp, 2e-2);
  assertClose("D_BOTTOM current", bottom.id * 1e6, expectedCurrentMicroAmp, 2e-2);

  console.log("u273 empirical MNA validation passed");
  console.log(`MID=${sol.MID.toFixed(6)} V`);
  console.log(`I=${(top.id * 1e6).toFixed(3)} uA`);
  console.log(`Expected direct law I(500mV)=${expectedCurrentMicroAmp.toFixed(3)} uA`);
}

if (require.main === module) {
  run();
}

module.exports = { run };
