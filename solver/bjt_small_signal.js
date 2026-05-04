"use strict";

const { c } = require("./complex_ac");

const THERMAL_VOLTAGE_300K = 25.85e-3;

function smallSignalParams(options) {
  const currentAmp = options.currentAmp;
  const beta = options.beta;
  const thermalVoltageVolt = options.thermalVoltageVolt ?? THERMAL_VOLTAGE_300K;
  const earlyVoltageVolt = options.earlyVoltageVolt ?? Infinity;

  if (!(currentAmp > 0)) throw new Error("currentAmp must be positive");
  if (!(beta > 0)) throw new Error("beta must be positive");
  if (!(thermalVoltageVolt > 0)) throw new Error("thermalVoltageVolt must be positive");

  const gmSiemens = currentAmp / thermalVoltageVolt;
  const rPiOhm = beta / gmSiemens;
  const emitterDynamicOhm = 1 / gmSiemens;
  const baseCurrentAmp = currentAmp / beta;
  const roOhm = Number.isFinite(earlyVoltageVolt) && earlyVoltageVolt > 0
    ? earlyVoltageVolt / currentAmp
    : Infinity;

  return {
    gmSiemens,
    rPiOhm,
    emitterDynamicOhm,
    baseCurrentAmp,
    roOhm,
  };
}

function hybridPiElements(options) {
  const name = options.name ?? "Q";
  const type = (options.type ?? "npn").toLowerCase();
  const collector = options.collector;
  const base = options.base;
  const emitter = options.emitter;
  const gmSiemens = options.gmSiemens;
  const rPiOhm = options.rPiOhm;
  const roOhm = options.roOhm ?? Infinity;

  if (!collector || !base || !emitter) throw new Error("collector, base and emitter are required");
  if (!(gmSiemens >= 0)) throw new Error("gmSiemens must be non-negative");
  if (!(rPiOhm > 0)) throw new Error("rPiOhm must be positive");
  if (!(type === "npn" || type === "pnp")) throw new Error("type must be npn or pnp");

  const admittances = [
    {
      name: `${name}_rpi`,
      n1: base,
      n2: emitter,
      y: c(1 / rPiOhm, 0),
    },
  ];

  if (Number.isFinite(roOhm) && roOhm > 0) {
    admittances.push({
      name: `${name}_ro`,
      n1: collector,
      n2: emitter,
      y: c(1 / roOhm, 0),
    });
  }

  const vccs = type === "npn"
    ? [{
      name: `${name}_gm`,
      nPlus: collector,
      nMinus: emitter,
      ctrlPlus: base,
      ctrlMinus: emitter,
      y: c(gmSiemens, 0),
    }]
    : [{
      name: `${name}_gm`,
      nPlus: emitter,
      nMinus: collector,
      ctrlPlus: emitter,
      ctrlMinus: base,
      y: c(gmSiemens, 0),
    }];

  return { admittances, vccs };
}

function appendHybridPi(netlist, options) {
  const elements = hybridPiElements(options);
  netlist.admittances = (netlist.admittances || []).concat(elements.admittances);
  netlist.vccs = (netlist.vccs || []).concat(elements.vccs);
  return netlist;
}

function appendHybridPiFromOperatingPoint(netlist, options) {
  const params = smallSignalParams(options);
  return appendHybridPi(netlist, {
    ...options,
    ...params,
  });
}

module.exports = {
  THERMAL_VOLTAGE_300K,
  smallSignalParams,
  hybridPiElements,
  appendHybridPi,
  appendHybridPiFromOperatingPoint,
};
