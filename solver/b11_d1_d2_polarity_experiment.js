"use strict";

const {
  s7ContactTable,
} = require("./b11_s7_detector_parametric");

const ENABLE_FLAG = "--enable-b11-d1-d2-polarity-experiment";
const SQRT2 = Math.sqrt(2);
const DEFAULT_R31_OHM = 51e3;
const DEFAULT_RECTIFIER_DROP_VOLT = 1.0;
const DEFAULT_D2_FORWARD_DROP_VOLT = 0.3;
const DEFAULT_D1_FORWARD_DROP_VOLT = 0.65;
const DEFAULT_ZL10_BV_VOLT = 10.0;

const d1PolarityCandidates = Object.freeze([
  {
    id: "D1_ZL10_GRAPHIC_CANDIDATE",
    component: "D1_ZL10",
    polarity: "graphic_zener_candidate",
    anode: "B11_NZENER_OUT_18_19",
    cathode: "B11_NCMD_LOCAL",
    model: "zener_with_reverse_breakdown_candidate",
    reverseBreakdownVolt: DEFAULT_ZL10_BV_VOLT,
    forwardDropVolt: DEFAULT_D1_FORWARD_DROP_VOLT,
    expectedLimiterContributionVolt: DEFAULT_ZL10_BV_VOLT,
  },
  {
    id: "D1_ZL10_REVERSED_CONTROL",
    component: "D1_ZL10",
    polarity: "reversed_control",
    anode: "B11_NCMD_LOCAL",
    cathode: "B11_NZENER_OUT_18_19",
    model: "forward_diode_control_no_zener_threshold_in_limiter_path",
    reverseBreakdownVolt: DEFAULT_ZL10_BV_VOLT,
    forwardDropVolt: DEFAULT_D1_FORWARD_DROP_VOLT,
    expectedLimiterContributionVolt: DEFAULT_D1_FORWARD_DROP_VOLT,
  },
]);

const d2PolarityCandidates = Object.freeze([
  {
    id: "D2_SSD55_GRAPHIC_CANDIDATE",
    component: "D2_SSD55",
    polarity: "graphic_detector_candidate",
    anode: "B11_N20_D2_TOP",
    cathode: "B11_ND2_BOT_RAW",
    model: "shockley_identity_only_candidate",
    forwardDropVolt: DEFAULT_D2_FORWARD_DROP_VOLT,
    conductsPositiveDetector: true,
  },
  {
    id: "D2_SSD55_REVERSED_CONTROL",
    component: "D2_SSD55",
    polarity: "reversed_control",
    anode: "B11_ND2_BOT_RAW",
    cathode: "B11_N20_D2_TOP",
    model: "shockley_reversed_control",
    forwardDropVolt: DEFAULT_D2_FORWARD_DROP_VOLT,
    conductsPositiveDetector: false,
  },
]);

function conductionMetricsFromThreshold(ueRms, thresholdPeakVolt, seriesOhm = DEFAULT_R31_OHM) {
  const peakVolt = ueRms * SQRT2;
  if (!Number.isFinite(thresholdPeakVolt) ||
      !(peakVolt > thresholdPeakVolt) ||
      !Number.isFinite(seriesOhm) ||
      seriesOhm <= 0) {
    return {
      ueRms,
      peakVolt,
      thresholdPeakVolt,
      thresholdRmsVolt: Number.isFinite(thresholdPeakVolt) ? thresholdPeakVolt / SQRT2 : Infinity,
      conductionDuty: 0,
      averageExcessVolt: 0,
      averageCurrentAmp: 0,
      peakCurrentAmp: 0,
    };
  }

  const theta0 = Math.asin(thresholdPeakVolt / peakVolt);
  const conductionDuty = 1 - 2 * theta0 / Math.PI;
  const halfWindow = Math.PI / 2 - theta0;
  const averageExcessVolt = (2 / Math.PI) *
    (peakVolt * Math.cos(theta0) - thresholdPeakVolt * halfWindow);
  const averageCurrentAmp = averageExcessVolt / seriesOhm;
  const peakCurrentAmp = (peakVolt - thresholdPeakVolt) / seriesOhm;

  return {
    ueRms,
    peakVolt,
    thresholdPeakVolt,
    thresholdRmsVolt: thresholdPeakVolt / SQRT2,
    conductionDuty,
    averageExcessVolt,
    averageCurrentAmp,
    peakCurrentAmp,
  };
}

function evaluatePolarityPair(d1, d2) {
  const positiveDetectorPathConducts = d2.conductsPositiveDetector === true;
  const d2Contribution = positiveDetectorPathConducts ? d2.forwardDropVolt : Infinity;
  const limiterThresholdPeakVolt =
    d2Contribution + DEFAULT_RECTIFIER_DROP_VOLT + d1.expectedLimiterContributionVolt;
  const compressorThresholdPeakVolt =
    d2Contribution + DEFAULT_RECTIFIER_DROP_VOLT;
  const thresholdSeparationPeakVolt =
    limiterThresholdPeakVolt - compressorThresholdPeakVolt;

  const checks = [
    {
      id: "D2_POSITIVE_RECTIFIED_DETECTOR_CONDUCTION",
      passed: positiveDetectorPathConducts,
      reason: positiveDetectorPathConducts
        ? "D2 graphic polarity conducts from B11_N20_D2_TOP to B11_ND2_BOT_RAW for the positive detector half-cycle."
        : "Reversed D2 blocks the positive detector half-cycle in this isolated functional probe.",
    },
    {
      id: "D1_LIMITER_ZENER_THRESHOLD_PRESENT",
      passed: thresholdSeparationPeakVolt >= 8.0 && thresholdSeparationPeakVolt <= 12.0,
      reason:
        "Limiter mode must add the ZL10-scale threshold relative to the S7 compressor bypass path.",
    },
    {
      id: "S7_COMPRESSOR_BYPASS_LOWERS_THRESHOLD",
      passed:
        Number.isFinite(limiterThresholdPeakVolt) &&
        Number.isFinite(compressorThresholdPeakVolt) &&
        compressorThresholdPeakVolt < limiterThresholdPeakVolt - 5.0,
      reason:
        "Use S7 limiter/compressor function as a rejection rule only; this does not promote the S7 contact truth table.",
    },
    {
      id: "B11_NCMD_LOCAL_BOUNDARY_HELD",
      passed: true,
      reason:
        "B11_NCMD_LOCAL remains an isolated local detector/control node and is not merged into the software B6 CMD node.",
    },
  ];

  const passedAll = checks.every((check) => check.passed);
  const sampleRows = [];
  for (const mode of ["limiter", "compressor"]) {
    const thresholdPeakVolt =
      mode === "limiter" ? limiterThresholdPeakVolt : compressorThresholdPeakVolt;
    for (const ueRms of [8, 12]) {
      sampleRows.push({
        mode,
        ueRms,
        ...conductionMetricsFromThreshold(ueRms, thresholdPeakVolt),
      });
    }
  }

  return {
    id: `${d1.id}__${d2.id}`,
    status: passedAll ? "FUNCTIONAL_PASS_GUARDED_NOT_PROMOTED" : "REJECTED_BY_FUNCTIONAL_POLARITY_RULES",
    d1,
    d2,
    limiterThresholdPeakVolt,
    limiterThresholdRmsVolt: Number.isFinite(limiterThresholdPeakVolt)
      ? limiterThresholdPeakVolt / SQRT2
      : Infinity,
    compressorThresholdPeakVolt,
    compressorThresholdRmsVolt: Number.isFinite(compressorThresholdPeakVolt)
      ? compressorThresholdPeakVolt / SQRT2
      : Infinity,
    thresholdSeparationPeakVolt,
    checks,
    sampleRows,
    spice_polarity_promoted: false,
    s7_contact_truth_table_promoted: false,
    b11_ncmd_local_separate_from_b6_cmd: true,
    mna_action: "isolated_functional_probe_only_not_stamped",
    promotion_limit:
      "A functional pass is still not a stamp. S7 contact routing, diode parameters, C11 transfer, active B11 output impedance and B11-to-B6 CMD KCL remain unproven.",
  };
}

function runPolarityExperiment() {
  const candidateRows = [];
  for (const d1 of d1PolarityCandidates) {
    for (const d2 of d2PolarityCandidates) {
      candidateRows.push(evaluatePolarityPair(d1, d2));
    }
  }

  const functionalPassRows = candidateRows.filter((row) =>
    row.status === "FUNCTIONAL_PASS_GUARDED_NOT_PROMOTED");
  const rejectedRows = candidateRows.filter((row) =>
    row.status === "REJECTED_BY_FUNCTIONAL_POLARITY_RULES");

  return {
    status: "GUARDED_EXPERIMENT_COMPLETE_NOT_PROMOTED",
    candidate_count: candidateRows.length,
    functional_pass_candidate_count: functionalPassRows.length,
    rejected_candidate_count: rejectedRows.length,
    accepted_for_future_guarded_mna_stamp:
      functionalPassRows.length === 1 ? functionalPassRows[0].id : null,
    candidate_rows: candidateRows,
    s7_functional_constraints_used: s7ContactTable.map((row) => ({
      position: row.position,
      closed: row.closed,
      open: row.open,
      zenerPath: row.zenerPath,
      inferredMode: row.inferredMode,
      promoted_contact_truth_table: false,
    })),
    required_next_proof: [
      "prove S7 17/18/19 closed/open contacts per mechanical position",
      "measure or source real SSD55 diode parameters before any official stamp",
      "keep B11_NCMD_LOCAL out of the B6 CMD KCL until the real B11 output path is proven",
      "run the later active B11 DC experiment with source/gmin stepping before replacing the Thevenin fixture",
    ],
    closure_verdict: {
      d1_graphic_polarity_functionally_consistent: functionalPassRows.some((row) =>
        row.d1.id === "D1_ZL10_GRAPHIC_CANDIDATE"),
      d2_graphic_polarity_functionally_consistent: functionalPassRows.some((row) =>
        row.d2.id === "D2_SSD55_GRAPHIC_CANDIDATE"),
      spice_polarity_promoted: false,
      active_b11_promotable: false,
      replace_thevenin_b11_s6_s7_cmd: false,
      s7_contact_truth_table_promoted: false,
      b11_ncmd_local_separate_from_b6_cmd: true,
    },
  };
}

function buildB11D1D2PolarityExperiment(options = {}) {
  const enabled = options.enableB11D1D2PolarityExperiment === true;
  return {
    id: "B11_D1_D2_POLARITY_EXPERIMENT",
    status: "CANDIDATE",
    enabled,
    enabled_by_default: false,
    explicit_enable_flag: ENABLE_FLAG,
    allowed_scope: "isolated_diode_polarity_functional_probe",
    isolated_from_executable_netlist: true,
    modifies_official_components: false,
    b11_ncmd_local_separate_from_b6_cmd: true,
    s7_contact_truth_table_promoted: false,
    d1_polarity_candidates: d1PolarityCandidates,
    d2_polarity_candidates: d2PolarityCandidates,
    result: enabled ? runPolarityExperiment() : null,
    blocked_outputs: [
      "official D1/D2 MNA stamping",
      "S7 truth-table promotion",
      "active B11 simulation",
      "B11_NCMD_LOCAL to B6 CMD merge",
      "replacement of B11_S6_S7.VB11_S6_S7 + RB11_S6_S7_CMD",
    ],
    boundary:
      "This experiment can reject diode polarity controls against S7 functional behavior, but it remains isolated from the executable netlist and cannot promote active B11.",
  };
}

module.exports = {
  ENABLE_FLAG,
  d1PolarityCandidates,
  d2PolarityCandidates,
  conductionMetricsFromThreshold,
  runPolarityExperiment,
  buildB11D1D2PolarityExperiment,
};
