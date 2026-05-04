"use strict";

const { c, abs, solveAc } = require("./complex_ac");

function assertClose(label, actual, expected, tolerance) {
  const error = Math.abs(actual - expected);
  if (error > tolerance) {
    throw new Error(`${label}: expected ${expected}, got ${actual}, error ${error}`);
  }
}

function db20(value) {
  return 20 * Math.log10(value);
}

function validateRcLowPass() {
  const r = 1000;
  const cap = 1e-6;
  const fc = 1 / (2 * Math.PI * r * cap);
  const w = 2 * Math.PI * fc;
  const netlist = {
    admittances: [
      { n1: "IN", n2: "OUT", y: c(1 / r, 0) },
      { n1: "OUT", n2: "0", y: c(0, w * cap) },
    ],
    voltageSources: [
      { name: "VIN", nPlus: "IN", nMinus: "0", value: c(1, 0) },
    ],
  };

  const result = solveAc(netlist);
  const gain = abs(result.solution.OUT);
  const expected = 1 / Math.sqrt(2);
  assertClose("RC low-pass cutoff magnitude", gain, expected, 1e-12);
  assertClose("RC low-pass cutoff dB", db20(gain), -3.010299956639812, 1e-10);

  return {
    fc,
    gain,
    gainDb: db20(gain),
    out: result.solution.OUT,
  };
}

function validateRcHighPass() {
  const r = 1000;
  const cap = 1e-6;
  const fc = 1 / (2 * Math.PI * r * cap);
  const w = 2 * Math.PI * fc;
  const netlist = {
    admittances: [
      { n1: "IN", n2: "OUT", y: c(0, w * cap) },
      { n1: "OUT", n2: "0", y: c(1 / r, 0) },
    ],
    voltageSources: [
      { name: "VIN", nPlus: "IN", nMinus: "0", value: c(1, 0) },
    ],
  };

  const result = solveAc(netlist);
  const gain = abs(result.solution.OUT);
  const expected = 1 / Math.sqrt(2);
  assertClose("RC high-pass cutoff magnitude", gain, expected, 1e-12);
  assertClose("RC high-pass cutoff dB", db20(gain), -3.010299956639812, 1e-10);

  return {
    fc,
    gain,
    gainDb: db20(gain),
    out: result.solution.OUT,
  };
}

function main() {
  const lowPass = validateRcLowPass();
  const highPass = validateRcHighPass();
  console.log("complex_ac validation passed");
  console.log(`RC cutoff frequency: ${lowPass.fc.toFixed(9)} Hz`);
  console.log(`Low-pass |Vout|: ${lowPass.gain.toFixed(15)} (${lowPass.gainDb.toFixed(12)} dB)`);
  console.log(`High-pass |Vout|: ${highPass.gain.toFixed(15)} (${highPass.gainDb.toFixed(12)} dB)`);
}

if (require.main === module) {
  main();
}

module.exports = {
  validateRcLowPass,
  validateRcHighPass,
};
