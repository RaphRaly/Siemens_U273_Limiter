"use strict";

const assert = require("node:assert");
const { solveDc, solutionObject } = require("./mna_core");

function close(actual, expected, tolerance, label) {
  const error = Math.abs(actual - expected);
  assert.ok(error <= tolerance, `${label}: expected ${expected}, got ${actual}, error ${error}`);
}

function voltageDividerTest() {
  const netlist = {
    voltageSources: [
      { name: "VIN", nPlus: "VIN", nMinus: "0", value: 10 },
    ],
    resistors: [
      { name: "R1", n1: "VIN", n2: "OUT", value: 10e3 },
      { name: "R2", n1: "OUT", n2: "0", value: 10e3 },
    ],
  };
  const result = solveDc(netlist);
  const sol = solutionObject(result, netlist);
  assert.equal(result.converged, true);
  close(sol.OUT, 5.0, 1e-9, "voltage divider OUT");
}

function diodeResistorTest() {
  const netlist = {
    voltageSources: [
      { name: "VIN", nPlus: "VIN", nMinus: "0", value: 1.0 },
    ],
    resistors: [
      { name: "R1", n1: "VIN", n2: "DOUT", value: 1e3 },
    ],
    diodes: [
      { name: "D1", anode: "DOUT", cathode: "0", is: 1e-12, n: 1.7, vt: 25.852e-3 },
    ],
  };
  const result = solveDc(netlist, { damping: 0.7, tolerance: 1e-10, maxIterations: 100 });
  const sol = solutionObject(result, netlist);
  assert.equal(result.converged, true);

  const rCurrent = (1.0 - sol.DOUT) / 1e3;
  const diode = result.diodeInfo[0];
  close(rCurrent, diode.id, 1e-8, "diode resistor KCL");
}

function run() {
  voltageDividerTest();
  diodeResistorTest();
  console.log("mna_core validation passed");
}

if (require.main === module) {
  run();
}
