"use strict";

// Empirical diode bridge law derived from the U273 research notes. Both JS MNA
// scripts and the C++ realtime model are validated against this shape.
const fs = require("node:fs");
const path = require("node:path");

const MICRO = 1e-6;
const MILLI = 1e-3;
const KILO = 1e3;

const LAW = Object.freeze({
  temperatureCelsius: 25,
  validCurrentMicroAmpMin: 2,
  validCurrentMicroAmpMax: 500,
  voltageFromCurrentCoefficientMilliVolt: 308,
  voltageFromCurrentExponent: 0.16,
  currentFromVoltageCoefficientMicroAmpPerMilliVoltPower: 2.85e-16,
  currentFromVoltageExponent: 6.25,
  dynamicResistanceCoefficientKiloOhm: 48.3,
  dynamicResistanceExponent: -0.84,
});

const VALIDATION_CURRENTS_MICRO_AMP = Object.freeze([
  2,
  5,
  10,
  18.33,
  50,
  100,
  250,
  500,
]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function assertPositiveFinite(value, name) {
  if (!(Number.isFinite(value) && value > 0)) {
    throw new Error(`${name} must be a positive finite number`);
  }
}

function diodeVoltageMilliVoltFromCurrentMicroAmp(currentMicroAmp) {
  assertPositiveFinite(currentMicroAmp, "currentMicroAmp");
  return LAW.voltageFromCurrentCoefficientMilliVolt *
    Math.pow(currentMicroAmp, LAW.voltageFromCurrentExponent);
}

function diodeCurrentMicroAmpFromVoltageMilliVolt(voltageMilliVolt) {
  assertPositiveFinite(voltageMilliVolt, "voltageMilliVolt");
  return LAW.currentFromVoltageCoefficientMicroAmpPerMilliVoltPower *
    Math.pow(voltageMilliVolt, LAW.currentFromVoltageExponent);
}

function dynamicResistanceKiloOhmFromCurrentMicroAmp(currentMicroAmp) {
  assertPositiveFinite(currentMicroAmp, "currentMicroAmp");
  return LAW.dynamicResistanceCoefficientKiloOhm *
    Math.pow(currentMicroAmp, LAW.dynamicResistanceExponent);
}

function dynamicConductanceSiemensFromCurrentMicroAmp(currentMicroAmp) {
  const resistanceOhm = dynamicResistanceKiloOhmFromCurrentMicroAmp(currentMicroAmp) * KILO;
  return 1 / resistanceOhm;
}

function operatingPointFromCurrentMicroAmp(currentMicroAmp) {
  const voltageMilliVolt = diodeVoltageMilliVoltFromCurrentMicroAmp(currentMicroAmp);
  const dynamicResistanceKiloOhm = dynamicResistanceKiloOhmFromCurrentMicroAmp(currentMicroAmp);
  const dynamicConductanceSiemens = 1 / (dynamicResistanceKiloOhm * KILO);
  const currentFromVoltageMicroAmp = diodeCurrentMicroAmpFromVoltageMilliVolt(voltageMilliVolt);

  return {
    currentMicroAmp,
    currentAmp: currentMicroAmp * MICRO,
    voltageMilliVolt,
    voltageVolt: voltageMilliVolt * MILLI,
    currentFromVoltageMicroAmp,
    inverseRelativeError: (currentFromVoltageMicroAmp - currentMicroAmp) / currentMicroAmp,
    dynamicResistanceKiloOhm,
    dynamicResistanceOhm: dynamicResistanceKiloOhm * KILO,
    dynamicConductanceSiemens,
    dynamicConductanceMicroSiemens: dynamicConductanceSiemens / MICRO,
    inStatedCurrentRange:
      currentMicroAmp >= LAW.validCurrentMicroAmpMin &&
      currentMicroAmp <= LAW.validCurrentMicroAmpMax,
  };
}

function operatingPointFromVoltageVolt(voltageVolt) {
  assertPositiveFinite(voltageVolt, "voltageVolt");
  const voltageMilliVolt = voltageVolt / MILLI;
  const currentMicroAmp = diodeCurrentMicroAmpFromVoltageMilliVolt(voltageMilliVolt);
  const dynamicResistanceKiloOhm = dynamicResistanceKiloOhmFromCurrentMicroAmp(currentMicroAmp);
  const dynamicConductanceSiemens = 1 / (dynamicResistanceKiloOhm * KILO);

  return {
    currentMicroAmp,
    currentAmp: currentMicroAmp * MICRO,
    voltageMilliVolt,
    voltageVolt,
    currentFromVoltageMicroAmp: currentMicroAmp,
    inverseRelativeError: 0,
    dynamicResistanceKiloOhm,
    dynamicResistanceOhm: dynamicResistanceKiloOhm * KILO,
    dynamicConductanceSiemens,
    dynamicConductanceMicroSiemens: dynamicConductanceSiemens / MICRO,
    inStatedCurrentRange:
      currentMicroAmp >= LAW.validCurrentMicroAmpMin &&
      currentMicroAmp <= LAW.validCurrentMicroAmpMax,
  };
}

function mnaLinearizationFromOperatingPoint(op) {
  const conductanceSiemens = op.dynamicConductanceSiemens;
  const equivalentCurrentAmp = op.currentAmp - conductanceSiemens * op.voltageVolt;

  return {
    ...op,
    conductanceSiemens,
    conductanceMicroSiemens: conductanceSiemens / MICRO,
    equivalentCurrentAmp,
    equivalentCurrentMicroAmp: equivalentCurrentAmp / MICRO,
    stampConvention:
      "Branch current from anode to cathode: I ~= G*(Vanode - Vcathode) + Ieq",
  };
}

function mnaLinearizationFromCurrentMicroAmp(currentMicroAmp) {
  return mnaLinearizationFromOperatingPoint(operatingPointFromCurrentMicroAmp(currentMicroAmp));
}

function mnaLinearizationFromVoltageVolt(voltageVolt) {
  return mnaLinearizationFromOperatingPoint(operatingPointFromVoltageVolt(voltageVolt));
}

function validationRows() {
  return VALIDATION_CURRENTS_MICRO_AMP.map((currentMicroAmp) => {
    const row = mnaLinearizationFromCurrentMicroAmp(currentMicroAmp);
    return {
      caseName: `ID_${currentMicroAmp}_uA`,
      ...row,
    };
  });
}

function siemensUsefulPoint() {
  const dc = mnaLinearizationFromVoltageVolt(1);
  const signalVolt = 25e-3;
  const lowVoltageVolt = dc.voltageVolt - signalVolt;
  const highVoltageVolt = dc.voltageVolt + signalVolt;
  const lowCurrentMicroAmp = diodeCurrentMicroAmpFromVoltageMilliVolt(lowVoltageVolt / MILLI);
  const highCurrentMicroAmp = diodeCurrentMicroAmpFromVoltageMilliVolt(highVoltageVolt / MILLI);
  const linearDeltaCurrentMicroAmp = dc.conductanceSiemens * signalVolt / MICRO;

  return {
    caseName: "siemens_local_branch_1Vdc_25mVac",
    note:
      "Local one-branch diode-law result only; no B6 topology or harmonic bridge formula is assumed here. The 1 V DC point is outside the stated 2..500 uA empirical current range.",
    dcVoltageVolt: dc.voltageVolt,
    dcCurrentMicroAmp: dc.currentMicroAmp,
    inStatedCurrentRange: dc.inStatedCurrentRange,
    dynamicResistanceKiloOhm: dc.dynamicResistanceKiloOhm,
    conductanceMicroSiemens: dc.conductanceMicroSiemens,
    equivalentCurrentMicroAmp: dc.equivalentCurrentMicroAmp,
    signalPeakVolt: signalVolt,
    linearDeltaCurrentPeakMicroAmp: linearDeltaCurrentMicroAmp,
    lowVoltageVolt,
    lowCurrentMicroAmp,
    highVoltageVolt,
    highCurrentMicroAmp,
    nonlinearDeltaLowMicroAmp: dc.currentMicroAmp - lowCurrentMicroAmp,
    nonlinearDeltaHighMicroAmp: highCurrentMicroAmp - dc.currentMicroAmp,
  };
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) {
    lines.push(columns.map((column) => row[column]).join(","));
  }
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
}

function run() {
  const rows = validationRows();
  const usefulPoint = siemensUsefulPoint();
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "u273_diode_empirical_validation.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 empirical composite diode law validation",
        status: "local diode branch law only; no bridge topology inferred",
        law: {
          voltageFromCurrent: "UD_mV = 308 * ID_uA^0.16",
          currentFromVoltage: "ID_uA = 2.85e-16 * UD_mV^6.25",
          dynamicResistance: "rd_kOhm = 48.3 * ID_uA^-0.84",
          dynamicConductance: "gd_S = 1 / (rd_kOhm * 1000)",
          mnaLinearization: "I = G*V + Ieq, Ieq = I0 - G*V0",
        },
        units: {
          UD: "mV in empirical equations, V in MNA stamp",
          ID: "uA in empirical equations, A in MNA stamp",
          rd: "kOhm in empirical equation",
          gd: "S in MNA stamp",
        },
        validationCurrentRangeMicroAmp: [
          LAW.validCurrentMicroAmpMin,
          LAW.validCurrentMicroAmpMax,
        ],
        validationRows: rows,
        siemensUsefulPoint: usefulPoint,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "u273_diode_empirical_validation.csv");
  writeCsv(csvPath, rows, [
    "caseName",
    "currentMicroAmp",
    "voltageMilliVolt",
    "currentFromVoltageMicroAmp",
    "inverseRelativeError",
    "dynamicResistanceKiloOhm",
    "dynamicConductanceMicroSiemens",
    "equivalentCurrentMicroAmp",
    "inStatedCurrentRange",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log("");
  for (const row of rows) {
    console.log(
      `${row.caseName}: UD=${row.voltageMilliVolt.toFixed(3)} mV, ` +
      `rd=${row.dynamicResistanceKiloOhm.toFixed(3)} kOhm, ` +
      `gd=${row.dynamicConductanceMicroSiemens.toFixed(3)} uS, ` +
      `Ieq=${row.equivalentCurrentMicroAmp.toFixed(3)} uA`
    );
  }
  console.log("");
  console.log(
    `${usefulPoint.caseName}: ID=${usefulPoint.dcCurrentMicroAmp.toFixed(3)} uA, ` +
    `rd=${usefulPoint.dynamicResistanceKiloOhm.toFixed(3)} kOhm, ` +
    `gd=${usefulPoint.conductanceMicroSiemens.toFixed(3)} uS, ` +
    `linear dI(25mV)=${usefulPoint.linearDeltaCurrentPeakMicroAmp.toFixed(3)} uA`
  );
}

if (require.main === module) {
  run();
}

module.exports = {
  LAW,
  VALIDATION_CURRENTS_MICRO_AMP,
  diodeVoltageMilliVoltFromCurrentMicroAmp,
  diodeCurrentMicroAmpFromVoltageMilliVolt,
  dynamicResistanceKiloOhmFromCurrentMicroAmp,
  dynamicConductanceSiemensFromCurrentMicroAmp,
  operatingPointFromCurrentMicroAmp,
  operatingPointFromVoltageVolt,
  mnaLinearizationFromOperatingPoint,
  mnaLinearizationFromCurrentMicroAmp,
  mnaLinearizationFromVoltageVolt,
  validationRows,
  siemensUsefulPoint,
  run,
};
