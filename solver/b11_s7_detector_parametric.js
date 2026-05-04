"use strict";

const fs = require("node:fs");
const path = require("node:path");

const SQRT2 = Math.sqrt(2);
const C11_FARAD = 500e-6;
const DEFAULT_SERIES_OHM = 51e3;

const s7ContactTable = Object.freeze([
  {
    position: "drawn_17_to_18",
    closed: "17-18",
    open: "17-19",
    zenerPath: "ZL10 active",
    inferredMode: "limiter",
    note: "User-confirmed drawing state: S7 common node 17 is switched to 18. D1 ZL10 remains in the limiter detector path.",
  },
  {
    position: "alternate_17_to_19",
    closed: "17-19",
    open: "17-18",
    zenerPath: "ZL10 bypassed",
    inferredMode: "compressor",
    note: "Opposite switch state inferred for compressor mode: S7 common node 17 is switched to 19, bypassing the ZL10 contribution.",
  },
]);

const detectorScenarios = Object.freeze([
  {
    name: "limiter_low_drop",
    mode: "limiter",
    s7Position: "drawn_17_to_18",
    s7Closed: "17-18",
    zenerPath: "ZL10 active",
    note: "Limiter bound with ZL10 active and low diode/rectifier drops.",
    zenerFraction: 1,
    zenerVolt: 10,
    diodeDropVolt: 0.2,
    rectifierDropVolt: 0.4,
    seriesOhm: DEFAULT_SERIES_OHM,
  },
  {
    name: "limiter_nominal",
    mode: "limiter",
    s7Position: "drawn_17_to_18",
    s7Closed: "17-18",
    zenerPath: "ZL10 active",
    note: "Nominal limiter hypothesis: ZL10 active, SSD55 plus rectifier drops chosen around the printed 8 V RMS threshold.",
    zenerFraction: 1,
    zenerVolt: 10,
    diodeDropVolt: 0.3,
    rectifierDropVolt: 1.0,
    seriesOhm: DEFAULT_SERIES_OHM,
  },
  {
    name: "limiter_high_drop",
    mode: "limiter",
    s7Position: "drawn_17_to_18",
    s7Closed: "17-18",
    zenerPath: "ZL10 active",
    note: "Limiter bound with ZL10 active and high diode/rectifier drops.",
    zenerFraction: 1,
    zenerVolt: 10,
    diodeDropVolt: 0.7,
    rectifierDropVolt: 1.4,
    seriesOhm: DEFAULT_SERIES_OHM,
  },
  {
    name: "limiter_zener_minus10pct",
    mode: "limiter",
    s7Position: "drawn_17_to_18",
    s7Closed: "17-18",
    zenerPath: "ZL10 active",
    note: "Sensitivity bound: ZL10 effective voltage 10 percent lower, nominal diode/rectifier drops.",
    zenerFraction: 1,
    zenerVolt: 9,
    diodeDropVolt: 0.3,
    rectifierDropVolt: 1.0,
    seriesOhm: DEFAULT_SERIES_OHM,
  },
  {
    name: "limiter_zener_plus10pct",
    mode: "limiter",
    s7Position: "drawn_17_to_18",
    s7Closed: "17-18",
    zenerPath: "ZL10 active",
    note: "Sensitivity bound: ZL10 effective voltage 10 percent higher, nominal diode/rectifier drops.",
    zenerFraction: 1,
    zenerVolt: 11,
    diodeDropVolt: 0.3,
    rectifierDropVolt: 1.0,
    seriesOhm: DEFAULT_SERIES_OHM,
  },
  {
    name: "compressor_partial_zener_bound",
    mode: "compressor",
    s7Position: "alternate_17_to_19_bound",
    s7Closed: "17-19",
    zenerPath: "ZL10 partly bypassed",
    note: "Conservative bound kept for sensitivity: S7 effectively leaves half of the ZL10 threshold in the path.",
    zenerFraction: 0.5,
    zenerVolt: 10,
    diodeDropVolt: 0.3,
    rectifierDropVolt: 1.0,
    seriesOhm: DEFAULT_SERIES_OHM,
  },
  {
    name: "compressor_s7_17_to_19_zener_bypassed",
    mode: "compressor",
    s7Position: "alternate_17_to_19",
    s7Closed: "17-19",
    zenerPath: "ZL10 bypassed",
    note: "Compressor hypothesis tied to the opposite S7 state: 17-19 bypasses the ZL10 contribution.",
    zenerFraction: 0,
    zenerVolt: 10,
    diodeDropVolt: 0.3,
    rectifierDropVolt: 1.0,
    seriesOhm: DEFAULT_SERIES_OHM,
  },
]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function thresholdPeakVolt(scenario) {
  return scenario.zenerFraction * scenario.zenerVolt +
    scenario.diodeDropVolt +
    scenario.rectifierDropVolt;
}

function thresholdRmsVolt(scenario) {
  return thresholdPeakVolt(scenario) / SQRT2;
}

function rmsToPeak(ueRms) {
  return ueRms * SQRT2;
}

function conductionMetrics(ueRms, scenario) {
  const peakVolt = rmsToPeak(ueRms);
  const thresholdVolt = thresholdPeakVolt(scenario);
  const seriesOhm = scenario.seriesOhm;

  if (!(peakVolt > thresholdVolt) || !Number.isFinite(seriesOhm) || seriesOhm <= 0) {
    return {
      ueRms,
      peakVolt,
      thresholdPeakVolt: thresholdVolt,
      thresholdRmsVolt: thresholdVolt / SQRT2,
      conductionDuty: 0,
      averageExcessVolt: 0,
      averageCurrentAmp: 0,
      peakCurrentAmp: 0,
      c11SlopeVoltPerSecond: 0,
    };
  }

  const theta0 = Math.asin(thresholdVolt / peakVolt);
  const conductionDuty = 1 - 2 * theta0 / Math.PI;
  const halfWindow = Math.PI / 2 - theta0;
  const averageExcessVolt = (2 / Math.PI) *
    (peakVolt * Math.cos(theta0) - thresholdVolt * halfWindow);
  const averageCurrentAmp = averageExcessVolt / seriesOhm;
  const peakCurrentAmp = (peakVolt - thresholdVolt) / seriesOhm;

  return {
    ueRms,
    peakVolt,
    thresholdPeakVolt: thresholdVolt,
    thresholdRmsVolt: thresholdVolt / SQRT2,
    conductionDuty,
    averageExcessVolt,
    averageCurrentAmp,
    peakCurrentAmp,
    c11SlopeVoltPerSecond: averageCurrentAmp / C11_FARAD,
  };
}

function numericConductionMetrics(ueRms, scenario, samples = 65536) {
  const peakVolt = rmsToPeak(ueRms);
  const thresholdVolt = thresholdPeakVolt(scenario);
  let sumExcess = 0;
  let countConducting = 0;
  let peakExcess = 0;

  for (let i = 0; i < samples; i += 1) {
    const theta = (i + 0.5) * 2 * Math.PI / samples;
    const excess = Math.max(0, peakVolt * Math.abs(Math.sin(theta)) - thresholdVolt);
    sumExcess += excess;
    if (excess > 0) countConducting += 1;
    if (excess > peakExcess) peakExcess = excess;
  }

  const averageExcessVolt = sumExcess / samples;
  return {
    averageExcessVolt,
    averageCurrentAmp: averageExcessVolt / scenario.seriesOhm,
    peakCurrentAmp: peakExcess / scenario.seriesOhm,
    conductionDuty: countConducting / samples,
  };
}

function makeThresholdRows() {
  const rows = [];
  for (const scenario of detectorScenarios) {
    for (let i = 0; i <= 64; i += 1) {
      const ueRms = i * 0.25;
      const metrics = conductionMetrics(ueRms, scenario);
      rows.push({
        scenario: scenario.name,
        mode: scenario.mode,
        s7Position: scenario.s7Position,
        s7Closed: scenario.s7Closed,
        zenerPath: scenario.zenerPath,
        note: scenario.note,
        zenerFraction: scenario.zenerFraction,
        zenerVolt: scenario.zenerVolt,
        diodeDropVolt: scenario.diodeDropVolt,
        rectifierDropVolt: scenario.rectifierDropVolt,
        seriesOhm: scenario.seriesOhm,
        ...metrics,
      });
    }
  }
  return rows;
}

function simulateEnvelope(scenario, ueHighRms, releaseOhm, options = {}) {
  const dt = options.dt ?? 0.001;
  const sampleEvery = options.sampleEvery ?? 0.01;
  const preSeconds = options.preSeconds ?? 0.1;
  const onSeconds = options.onSeconds ?? 1.0;
  const offSeconds = options.offSeconds ?? 2.0;
  const totalSeconds = preSeconds + onSeconds + offSeconds;
  const tauSeconds = releaseOhm * C11_FARAD;
  const rows = [];
  let vCap = 0;
  let nextSample = 0;

  for (let step = 0; step <= Math.round(totalSeconds / dt); step += 1) {
    const time = step * dt;
    const ueRms = time >= preSeconds && time < preSeconds + onSeconds ? ueHighRms : 0;
    const metrics = conductionMetrics(ueRms, scenario);
    const targetVolt = metrics.averageCurrentAmp * releaseOhm;
    const alpha = Math.exp(-dt / tauSeconds);
    vCap = targetVolt + (vCap - targetVolt) * alpha;

    if (time + 1e-12 >= nextSample || step === 0 || step === Math.round(totalSeconds / dt)) {
      rows.push({
        scenario: scenario.name,
        mode: scenario.mode,
        ueHighRms,
        releaseOhm,
        tauSeconds,
        time,
        ueRms,
        averageCurrentAmp: metrics.averageCurrentAmp,
        vCapEquivalentVolt: vCap,
      });
      nextSample += sampleEvery;
    }
  }

  const at = (seconds) => {
    let best = rows[0];
    for (const row of rows) {
      if (Math.abs(row.time - seconds) < Math.abs(best.time - seconds)) best = row;
    }
    return best;
  };

  return {
    rows,
    summary: {
      scenario: scenario.name,
      mode: scenario.mode,
      ueHighRms,
      releaseOhm,
      tauSeconds,
      vAtStartVolt: at(preSeconds).vCapEquivalentVolt,
      vAfter100msOnVolt: at(preSeconds + 0.1).vCapEquivalentVolt,
      vAfter500msOnVolt: at(preSeconds + 0.5).vCapEquivalentVolt,
      vAtEndOfOnVolt: at(preSeconds + onSeconds).vCapEquivalentVolt,
      vAfter500msReleaseVolt: at(preSeconds + onSeconds + 0.5).vCapEquivalentVolt,
      vAtEndVolt: at(totalSeconds).vCapEquivalentVolt,
    },
  };
}

function makeStepRows() {
  const scenarioNames = new Set(["limiter_nominal", "compressor_s7_17_to_19_zener_bypassed"]);
  const scenarios = detectorScenarios.filter((scenario) => scenarioNames.has(scenario.name));
  const ueHighValues = [8, 12];
  const releaseOhms = [1e3, 1e4, 1e5];
  const rows = [];
  const summaries = [];

  for (const scenario of scenarios) {
    for (const ueHighRms of ueHighValues) {
      for (const releaseOhm of releaseOhms) {
        const response = simulateEnvelope(scenario, ueHighRms, releaseOhm);
        rows.push(...response.rows);
        summaries.push(response.summary);
      }
    }
  }

  return { rows, summaries };
}

function makeNumericChecks() {
  const checks = [];
  for (const scenario of detectorScenarios) {
    for (const ueRms of [thresholdRmsVolt(scenario), 8, 12, 16]) {
      const analytic = conductionMetrics(ueRms, scenario);
      const numeric = numericConductionMetrics(ueRms, scenario);
      checks.push({
        scenario: scenario.name,
        ueRms,
        averageCurrentAbsErrorAmp: Math.abs(analytic.averageCurrentAmp - numeric.averageCurrentAmp),
        peakCurrentAbsErrorAmp: Math.abs(analytic.peakCurrentAmp - numeric.peakCurrentAmp),
        dutyAbsError: Math.abs(analytic.conductionDuty - numeric.conductionDuty),
      });
    }
  }
  return checks;
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) {
    lines.push(columns.map((column) => row[column]).join(","));
  }
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
}

function runSweep() {
  const thresholdRows = makeThresholdRows();
  const step = makeStepRows();
  const numericChecks = makeNumericChecks();
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b11_s7_detector_parametric.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11/S7 detector parametric threshold sweep",
        status: "S7 17-19/17-18 contact function is encoded; diode curves and active B11 loading remain parametric",
        warning: "C11 is modeled here as an equivalent storage capacitor with a swept release load. This is not yet the full B11 transistor regulator.",
        s7ContactTable,
        equations: {
          peak: "Vpeak = sqrt(2) * Ue_rms",
          threshold: "Vth = k_zener * Vz + V_D2 + V_rect",
          instantaneousCurrent: "i(theta) = max(0, Vpeak*abs(sin(theta)) - Vth) / R31",
          capacitor: "dVcap/dt = (Iavg - Vcap/Rrelease) / C11",
        },
        constants: {
          c11Farad: C11_FARAD,
          r31Ohm: DEFAULT_SERIES_OHM,
        },
        detectorScenarios,
        thresholdRows,
        stepRows: step.rows,
        stepSummaries: step.summaries,
        numericChecks,
      },
      null,
      2
    )
  );

  const thresholdCsvPath = path.join(outDir, "b11_s7_detector_threshold_sweep.csv");
  writeCsv(thresholdCsvPath, thresholdRows, [
    "scenario",
    "mode",
    "s7Position",
    "s7Closed",
    "zenerPath",
    "zenerFraction",
    "zenerVolt",
    "diodeDropVolt",
    "rectifierDropVolt",
    "seriesOhm",
    "thresholdPeakVolt",
    "thresholdRmsVolt",
    "ueRms",
    "peakVolt",
    "conductionDuty",
    "averageExcessVolt",
    "averageCurrentAmp",
    "peakCurrentAmp",
    "c11SlopeVoltPerSecond",
  ]);

  const stepCsvPath = path.join(outDir, "b11_s7_detector_step_response.csv");
  writeCsv(stepCsvPath, step.rows, [
    "scenario",
    "mode",
    "ueHighRms",
    "releaseOhm",
    "tauSeconds",
    "time",
    "ueRms",
    "averageCurrentAmp",
    "vCapEquivalentVolt",
  ]);

  const summaryCsvPath = path.join(outDir, "b11_s7_detector_step_summary.csv");
  writeCsv(summaryCsvPath, step.summaries, [
    "scenario",
    "mode",
    "ueHighRms",
    "releaseOhm",
    "tauSeconds",
    "vAtStartVolt",
    "vAfter100msOnVolt",
    "vAfter500msOnVolt",
    "vAtEndOfOnVolt",
    "vAfter500msReleaseVolt",
    "vAtEndVolt",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${thresholdCsvPath}`);
  console.log(`Wrote ${stepCsvPath}`);
  console.log(`Wrote ${summaryCsvPath}`);
  console.log("");
  for (const scenario of detectorScenarios) {
    const at8 = conductionMetrics(8, scenario);
    const at12 = conductionMetrics(12, scenario);
    console.log(
      `${scenario.name}: Vth=${thresholdPeakVolt(scenario).toFixed(2)} Vpk ` +
      `(${thresholdRmsVolt(scenario).toFixed(2)} Vrms), ` +
      `Iavg@8Vrms=${(at8.averageCurrentAmp * 1e6).toFixed(2)} uA, ` +
      `Iavg@12Vrms=${(at12.averageCurrentAmp * 1e6).toFixed(2)} uA`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  s7ContactTable,
  detectorScenarios,
  thresholdPeakVolt,
  thresholdRmsVolt,
  conductionMetrics,
  numericConductionMetrics,
  simulateEnvelope,
  runSweep,
};
