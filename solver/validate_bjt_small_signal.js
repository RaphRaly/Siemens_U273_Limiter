"use strict";

const { c, abs, solveAc } = require("./complex_ac");
const {
  smallSignalParams,
  appendHybridPi,
  appendHybridPiFromOperatingPoint,
} = require("./bjt_small_signal");

function assertClose(label, actual, expected, tolerance) {
  const error = Math.abs(actual - expected);
  if (error > tolerance) {
    throw new Error(`${label}: expected ${expected}, got ${actual}, error ${error}`);
  }
}

function parallel(a, b) {
  return 1 / (1 / a + 1 / b);
}

function validateSmallSignalParams() {
  const params = smallSignalParams({
    currentAmp: 1e-3,
    beta: 100,
    thermalVoltageVolt: 25.85e-3,
  });

  assertClose("gm at 1 mA", params.gmSiemens, 0.001 / 0.02585, 1e-15);
  assertClose("re at 1 mA", params.emitterDynamicOhm, 25.85, 1e-12);
  assertClose("rpi at beta 100, 1 mA", params.rPiOhm, 2585, 1e-9);
  assertClose("base current", params.baseCurrentAmp, 10e-6, 1e-18);

  return params;
}

function validateCommonEmitterNoRo() {
  const gm = 40e-3;
  const rPi = 2500;
  const rc = 1000;
  const vin = 1e-3;
  const netlist = {
    admittances: [
      { name: "RC", n1: "C", n2: "0", y: c(1 / rc, 0) },
    ],
    voltageSources: [
      { name: "VIN", nPlus: "B", nMinus: "0", value: c(vin, 0) },
    ],
  };

  appendHybridPi(netlist, {
    name: "Q1",
    type: "npn",
    collector: "C",
    base: "B",
    emitter: "0",
    gmSiemens: gm,
    rPiOhm: rPi,
  });

  const result = solveAc(netlist);
  const vout = result.solution.C.re;
  const expected = -gm * rc * vin;
  assertClose("common-emitter gain without ro", vout, expected, 1e-14);
  return { vout, expected, gain: vout / vin, expectedGain: -gm * rc };
}

function validateCommonEmitterWithRo() {
  const gm = 40e-3;
  const rPi = 2500;
  const rc = 1000;
  const ro = 10000;
  const vin = 1e-3;
  const rload = parallel(rc, ro);
  const netlist = {
    admittances: [
      { name: "RC", n1: "C", n2: "0", y: c(1 / rc, 0) },
    ],
    voltageSources: [
      { name: "VIN", nPlus: "B", nMinus: "0", value: c(vin, 0) },
    ],
  };

  appendHybridPi(netlist, {
    name: "Q1",
    type: "npn",
    collector: "C",
    base: "B",
    emitter: "0",
    gmSiemens: gm,
    rPiOhm: rPi,
    roOhm: ro,
  });

  const result = solveAc(netlist);
  const vout = result.solution.C.re;
  const expected = -gm * rload * vin;
  assertClose("common-emitter gain with ro", vout, expected, 1e-14);
  return { vout, expected, gain: vout / vin, expectedGain: -gm * rload };
}

function validateOperatingPointHelper() {
  const rc = 1000;
  const vin = 1e-3;
  const currentAmp = 1e-3;
  const beta = 100;
  const gm = currentAmp / 25.85e-3;
  const netlist = {
    admittances: [
      { name: "RC", n1: "C", n2: "0", y: c(1 / rc, 0) },
    ],
    voltageSources: [
      { name: "VIN", nPlus: "B", nMinus: "0", value: c(vin, 0) },
    ],
  };

  appendHybridPiFromOperatingPoint(netlist, {
    name: "QOP",
    type: "npn",
    collector: "C",
    base: "B",
    emitter: "0",
    currentAmp,
    beta,
  });

  const result = solveAc(netlist);
  const vout = result.solution.C.re;
  const expected = -gm * rc * vin;
  assertClose("operating point helper gain", vout, expected, 1e-14);
  return { vout, expected, gain: vout / vin };
}

function run() {
  const params = validateSmallSignalParams();
  const noRo = validateCommonEmitterNoRo();
  const withRo = validateCommonEmitterWithRo();
  const helper = validateOperatingPointHelper();

  console.log("bjt_small_signal validation passed");
  console.log(`gm@1mA: ${(params.gmSiemens * 1e3).toFixed(6)} mS`);
  console.log(`re@1mA: ${params.emitterDynamicOhm.toFixed(6)} Ohm`);
  console.log(`rpi@beta100: ${params.rPiOhm.toFixed(6)} Ohm`);
  console.log(`CE gain no ro: ${noRo.gain.toFixed(9)} expected ${noRo.expectedGain.toFixed(9)}`);
  console.log(`CE gain with ro: ${withRo.gain.toFixed(9)} expected ${withRo.expectedGain.toFixed(9)}`);
  console.log(`OP helper gain: ${helper.gain.toFixed(9)}`);
}

if (require.main === module) {
  run();
}

module.exports = {
  validateSmallSignalParams,
  validateCommonEmitterNoRo,
  validateCommonEmitterWithRo,
  validateOperatingPointHelper,
  run,
};
