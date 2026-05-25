"use strict";

// High-level research orchestrator: combines B6, B11/S6/S7 hypotheses into a
// generated netlist and preserves unresolved scientific boundaries explicitly.
const fs = require("node:fs");
const path = require("node:path");
const { solveDc, solutionObject } = require("./mna_core");
const {
  buildB6ParametricDcNetlist,
  buildB6CompleteSchematicInventory,
  resolveB6Params,
} = require("./b6_parametric");
const {
  values: s6CoreValues,
  contactHypotheses: s6ContactHypotheses,
} = require("./b11_s6_zout_sweep");
const {
  s7ContactTable,
  detectorScenarios,
} = require("./b11_s7_detector_parametric");
const {
  buildB11D1D2PolarityExperiment,
} = require("./b11_d1_d2_polarity_experiment");
const {
  V: b11CropVoltages,
  R: b11CropResistors,
  C: b11CropCapacitors,
  passiveKcl: b11CropPassiveKcl,
  topologyRows: b11CropTopologyRows,
} = require("./b11_crop_ts1_ts2_corrected_topology");
const {
  LAW: u273EmpiricalDiodeLaw,
} = require("./u273_diode_empirical");
const {
  passiveChecks: b6PassiveChecks,
  transistorHybridPiRows: b6TransistorHybridPiRows,
  outputStageBounds: b6OutputStageBounds,
  symbolicTransistorBoundaries: b6SymbolicTransistorBoundaries,
} = require("./b6_active_hybrid_pi_inventory");

const STATUS = Object.freeze({
  CLOSED: "FERME",
  PARTIAL: "PARTIEL",
  PARAMETRIC: "PARAMETRIQUE",
  HYPOTHESIS: "HYPOTHESE",
  CANDIDATE: "CANDIDATE",
  SWITCH_CONTACT_CANDIDATE: "SWITCH_CONTACT_CANDIDATE",
  STRONG_CANDIDATE: "CANDIDAT_FORT",
  SCHEMATIC_CANDIDATE_PARTIAL: "CANDIDAT_SCHEMA_PARTIEL",
  PDF_VISUAL_CONFIRMED: "PDF_VISUAL_CONFIRMED",
  PDF_VISUAL_CONFIRMED_CANDIDATE: "PDF_VISUAL_CONFIRMED_CANDIDATE",
  PDF_TEXT_FUNCTIONAL_CONFIRMED: "PDF_TEXT_FUNCTIONAL_CONFIRMED",
  UNKNOWN: "UNKNOWN",
  UNCONFIRMED: "UNCONFIRMED",
  SUPERSEDED: "SUPERSEDED_FOR_STEP1",
  CONFLICT: "CONFLICTS_WITH_STEP1_CANONICAL_TOPOLOGY",
  NON_READ: "NON_LU",
  NUMERICAL: "NUMERICAL_HELPER",
});

const DEFAULTS = Object.freeze({
  debugConfigId: "U273_DEBUG_CONFIG_001",
  debugConfigMode: "limiter_drawn_position",
  outputLoadOhm: 300,
  supplyVolt: 24,
  couplingMode: "thevenin_b11_s6_s7_reference",
  s6Position: "delivery_linear",
  s7Mode: "limiter_drawn_17_to_18",
  commandSourceVolt: 1,
  commandSourceOhm: 10e3,
  bridgeDiodeLaw: "u273_empirical",
  r7EffectiveOhm: 100,
  r8EffectiveOhm: 250e3,
  c4AbglFarad: 0,
  c6AbglFarad: 0,
  rAmpInputOhm: Infinity,
});

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function finiteOrString(value) {
  return Number.isFinite(value) ? value : String(value);
}

function resolveOptions(options = {}) {
  const commandSourceVolt = options.commandSourceVolt ?? DEFAULTS.commandSourceVolt;
  const b6 = {
    bridgeDiodeLaw: options.bridgeDiodeLaw ?? DEFAULTS.bridgeDiodeLaw,
    commandDcMode: "thevenin",
    commandSourceVolt,
    commandSourceOhm: options.commandSourceOhm ?? DEFAULTS.commandSourceOhm,
    r7EffectiveOhm: options.r7EffectiveOhm ?? DEFAULTS.r7EffectiveOhm,
    r8EffectiveOhm: options.r8EffectiveOhm ?? DEFAULTS.r8EffectiveOhm,
    c4AbglFarad: options.c4AbglFarad ?? DEFAULTS.c4AbglFarad,
    c6AbglFarad: options.c6AbglFarad ?? DEFAULTS.c6AbglFarad,
    rAmpInputOhm: options.rAmpInputOhm ?? DEFAULTS.rAmpInputOhm,
  };

  return {
    debugConfigId: options.debugConfigId ?? DEFAULTS.debugConfigId,
    debugConfigMode: options.debugConfigMode ?? DEFAULTS.debugConfigMode,
    outputLoadOhm: options.outputLoadOhm ?? DEFAULTS.outputLoadOhm,
    supplyVolt: options.supplyVolt ?? DEFAULTS.supplyVolt,
    couplingMode: options.couplingMode ?? DEFAULTS.couplingMode,
    s6Position: options.s6Position ?? DEFAULTS.s6Position,
    s7Mode: options.s7Mode ?? DEFAULTS.s7Mode,
    commandSourceVolt,
    b6,
  };
}

function b6OutputBiasDcCheckpoint() {
  const r33 = 20e3;
  const r34 = 3.6e3;
  const r35 = 820;
  const r36 = 24;
  const v22 = 22.0;
  const n215 = 21.5;
  const n105 = 10.5;
  const outputBiasLeft =
    (n215 / r33 + n105 / r34) /
    (1 / r33 + 1 / r34 + 1 / r35);
  const ir33FromN215ToBiasAmp = (n215 - outputBiasLeft) / r33;
  const ir34FromN105ToBiasAmp = (n105 - outputBiasLeft) / r34;
  const ir35FromBiasToGroundAmp = outputBiasLeft / r35;
  const ir36FromV22ToN215Amp = (v22 - n215) / r36;

  return {
    id: "B6_OUTPUT_BIAS_LEFT_KCL",
    status: STATUS.CANDIDATE,
    source: "Siemens_U273_Limiter.pdf page 1 visual schematic; printed voltages used as checkpoints",
    locked_topology: [
      "R36: V22 -> N215",
      "R33: OUTPUT_BIAS_LEFT -> N215",
      "C20: OUTPUT_BIAS_LEFT -> N215",
      "R34: OUTPUT_BIAS_LEFT -> N105",
      "C21: N105 -> U2_PRI_TOP",
      "U2 primary starts after C21",
    ],
    printed_voltages: {
      V22: v22,
      N215: n215,
      N105: n105,
      OUTPUT_BIAS_LEFT_SOLVED_FROM_PASSIVE_KCL: outputBiasLeft,
    },
    currents_amp: {
      ir33FromN215ToBiasAmp,
      ir34FromN105ToBiasAmp,
      ir35FromBiasToGroundAmp,
      ir36FromV22ToN215Amp,
    },
    kcl_residual_amp: ir33FromN215ToBiasAmp + ir34FromN105ToBiasAmp - ir35FromBiasToGroundAmp,
    boundary:
      "This is a local passive KCL checkpoint only. It does not prove Ts5/Ts6 pinout or the active output stage.",
  };
}

function buildDebugConfig(resolved) {
  return {
    id: resolved.debugConfigId,
    status: "PARTIAL_TOPOLOGY_SAFE_DC_CLOSURE",
    mode: resolved.debugConfigMode,
    source_of_truth: {
      pdf: "Siemens_U273_Limiter.pdf",
      page: 1,
      treatment: "visual schematic proof; OCR text is not trusted",
    },
    supply_volt: resolved.supplyVolt,
    output_load_ohm: resolved.outputLoadOhm,
    selected_switch_positions: {
      S3: {
        status: STATUS.SWITCH_CONTACT_CANDIDATE,
        target: "drawn output position, nominal 300 ohm output",
        truth_table_status: STATUS.UNKNOWN,
      },
      S4: {
        status: STATUS.SWITCH_CONTACT_CANDIDATE,
        target: "drawn output return position, nominal 300 ohm output",
        truth_table_status: STATUS.UNKNOWN,
      },
      S6: {
        status: STATUS.SWITCH_CONTACT_CANDIDATE,
        selected_position: resolved.s6Position,
        truth_table_status: STATUS.UNKNOWN,
      },
      S7: {
        status: STATUS.SWITCH_CONTACT_CANDIDATE,
        selected_mode: resolved.s7Mode,
        target: "drawn limiter/Begrenzer position",
        truth_table_status: STATUS.UNKNOWN,
      },
    },
    potentiometers: {
      representation: "three-terminal explicit components where present",
      default_wiper_fraction: 0.5,
      policy: "debug default only; not a calibrated Siemens setting",
    },
    locked_now: [
      "B6 corrected output topology around R36/R33/C20/R34/C21/U2",
      "C21 is a series coupling capacitor from N105 to U2_PRI_TOP",
      "B6 output passive KCL checkpoint can be evaluated from printed voltages",
    ],
    not_locked: [
      "S3/S4/S6/S7 complete mechanical truth tables",
      "U1/U2 ratios and winding parasitics",
      "B6 Ts1/Ts3/Ts5/Ts6 and all B11 transistor pinouts",
      "B11 full active regulator loop",
      "junction capacitances, charge storage, transformer dynamics",
    ],
  };
}

function switchContactCandidate(resolved, entry) {
  return {
    id: entry.id,
    switch: entry.switch,
    selected_config: resolved.debugConfigId,
    selected_mode: resolved.debugConfigMode,
    selected_position: entry.selected_position,
    terminals: entry.terminals,
    contact_state: entry.contact_state,
    status: STATUS.SWITCH_CONTACT_CANDIDATE,
    truth_table_status: STATUS.UNKNOWN,
    evidence: entry.evidence,
    mna_action: "boundary_only_not_stamped",
    boundary:
      "Drawn/debug contact candidate only. This is not a complete mechanical switch truth table and must not be promoted without visual continuity proof.",
  };
}

function buildSwitchContactCandidates(resolved) {
  const entries = [
    {
      id: "S3_DRAWN_22_23_CLOSED",
      switch: "S3",
      selected_position: "drawn_output_300_ohm",
      terminals: ["22", "23"],
      contact_state: "candidate_closed",
      evidence: "B6 output switch upper section in the current Etape 26 schematic transcription.",
    },
    {
      id: "S4_DRAWN_25_26_CLOSED",
      switch: "S4",
      selected_position: "drawn_output_300_ohm",
      terminals: ["25", "26"],
      contact_state: "candidate_closed",
      evidence: "B6 output switch lower section in the current Etape 26 schematic transcription.",
    },
    {
      id: "S6_DELIVERY_4_5_CLOSED",
      switch: "S6",
      selected_position: resolved.s6Position,
      terminals: ["4", "5"],
      contact_state: "candidate_closed",
      evidence: "Provisional delivery-linear S6 reading from Etape 20/21/24.",
    },
    {
      id: "S6_DELIVERY_8_9_CLOSED",
      switch: "S6",
      selected_position: resolved.s6Position,
      terminals: ["8", "9"],
      contact_state: "candidate_closed",
      evidence: "Provisional delivery-linear S6 reading from Etape 20/21/24.",
    },
    {
      id: "S6_DELIVERY_5_6_OPEN",
      switch: "S6",
      selected_position: resolved.s6Position,
      terminals: ["5", "6"],
      contact_state: "candidate_open",
      evidence: "Provisional delivery-linear S6 reading from Etape 20/21/24.",
    },
    {
      id: "S6_DELIVERY_7_8_OPEN",
      switch: "S6",
      selected_position: resolved.s6Position,
      terminals: ["7", "8"],
      contact_state: "candidate_open",
      evidence: "Provisional delivery-linear S6 reading from Etape 20/21/24.",
    },
    {
      id: "S6_DELIVERY_5_3_1_CHAIN_CLOSED",
      switch: "S6",
      selected_position: resolved.s6Position,
      terminals: ["5", "3", "1"],
      contact_state: "candidate_closed_chain",
      evidence: "Provisional delivery-linear S6 reading from Etape 20/21/24; represented as a chain, not as a proven switch wafer truth table.",
    },
    {
      id: "S6_DELIVERY_2_OPEN",
      switch: "S6",
      selected_position: resolved.s6Position,
      terminals: ["2"],
      contact_state: "candidate_open_unconnected",
      evidence: "Provisional delivery-linear S6 reading from Etape 20/21/24.",
    },
    {
      id: "S7_DRAWN_17_18_CLOSED",
      switch: "S7",
      selected_position: resolved.s7Mode,
      terminals: ["17", "18"],
      contact_state: "candidate_closed",
      evidence: "Drawn limiter/Begrenzer S7 state from the current S7 contact table.",
    },
    {
      id: "S7_DRAWN_17_19_OPEN",
      switch: "S7",
      selected_position: resolved.s7Mode,
      terminals: ["17", "19"],
      contact_state: "candidate_open",
      evidence: "Complementary open contact in the drawn limiter/Begrenzer S7 state.",
    },
  ];

  return {
    id: `${resolved.debugConfigId}.SWITCH_CONTACT_CANDIDATES`,
    status: STATUS.SWITCH_CONTACT_CANDIDATE,
    truth_table_status: STATUS.UNKNOWN,
    applied_to_executable_netlist: false,
    boundary:
      "Contacts are encoded as explicit candidates for review only. The executable DC path still uses guarded component boundaries and the B11/S6/S7 Thevenin command source.",
    contacts: entries.map((entry) => switchContactCandidate(resolved, entry)),
  };
}

function buildB11S6S7SwitchMatrixCandidate(resolved) {
  return {
    id: "B11_S6_S7_SWITCH_MATRIX_CANDIDATE_STEP1",
    status: STATUS.SWITCH_CONTACT_CANDIDATE,
    switch_matrix_status: "SWITCH_MATRIX_CANDIDATE",
    truth_table_status: STATUS.UNKNOWN,
    selected_debug_position: {
      S6: resolved.s6Position,
      S7: resolved.s7Mode,
    },
    executable_netlist_effect:
      "none; this is a switch-matrix closure checklist and does not alter the official B11/S6/S7 Thevenin boundary",
    known_network_values: {
      R3_ohm: s6CoreValues.r3,
      R4_ohm: s6CoreValues.r4,
      R5_ohm: s6CoreValues.r5,
      R6_ohm: s6CoreValues.r6,
      C1_farad: s6CoreValues.c1,
      C2_farad: s6CoreValues.c2,
      C3_farad: s6CoreValues.c3,
      C4_farad: s6CoreValues.c4,
      R1_ohm: 3e3,
      R2_user_candidate_ohm: 1e3,
    },
    value_boundaries: [
      {
        component: "R2",
        candidate_ohm: 1e3,
        status: STATUS.CANDIDATE,
        promotion_limit:
          "User/GPT note proposes 1k. Prior local crop notes saw an ambiguous high-value R2 branch, so R2 must be re-read before executable stamping.",
      },
    ],
    possible_topology_effects: [
      "preemphasis_bypassed_linear",
      "preemphasis_partially_inserted",
      "preemphasis_fully_inserted",
      "connected_to_detector_command_path",
      "isolated_from_detector_command_path",
    ],
    forbidden_shortcuts: [
      "S6 = mode_limiter",
      "S7 = mode_compressor",
      "single global S6/S7 truth table without per-contact proof",
      "Thevenin replacement from a selected debug contact only",
    ],
    per_position_required_fields: [
      "mechanical_position",
      "contact_closed",
      "contact_open",
      "left_node",
      "right_node",
      "effect_on_preemphasis",
      "effect_on_CMD_D1_D2",
      "evidence_crop",
      "promotion_decision",
    ],
    currently_encoded_contacts_are_candidates_only: true,
    contact_candidates_source: `${resolved.debugConfigId}.SWITCH_CONTACT_CANDIDATES`,
    activation_criterion:
      "Every mechanical position must have closed/open contacts and left/right nodes proven before S6/S7 can drive active B11 CMD routing.",
    boundary:
      "S6/S7 remain a switch matrix candidate. Values can be used as passive network constants, but modes and command routing are not promoted.",
  };
}

function buildB11VisualInventory() {
  const passive = b11CropPassiveKcl();
  const candidateRows = b11CropTopologyRows().map((row) => ({
    item: row.item,
    visible_connection: row.visibleConnection,
    proposed_dc_stamp: row.dcStamp,
    source_status: row.status,
    status: STATUS.CANDIDATE,
    mna_action: "inventory_only_not_stamped",
    boundary:
      "B11 visual inventory candidate only. Do not use as an active MNA stamp until the full B11 topology and BJT pins are proven.",
  }));

  return {
    id: "B11_VISUAL_INVENTORY_STEP1",
    status: STATUS.CANDIDATE,
    source: "solver/b11_crop_ts1_ts2_corrected_topology.js plus generated B11 result artifacts",
    source_limits:
      "Existing local B11 crop scripts are useful evidence, but they are not a complete PDF-wide B11 netlist proof.",
    active_simulation_allowed_for_step1: false,
    mna_action: "inventory_only_not_stamped",
    printed_voltages: {
      V24: b11CropVoltages.V24,
      N215: b11CropVoltages.N215,
      N145: b11CropVoltages.N145,
      N10: b11CropVoltages.N10,
      N9: b11CropVoltages.N9,
      N05: b11CropVoltages.N05,
      REF: b11CropVoltages.REF,
    },
    resistor_candidates_ohm: {
      R10: b11CropResistors.R10,
      R11: b11CropResistors.R11,
      R13: b11CropResistors.R13,
      R14: b11CropResistors.R14,
      R15: b11CropResistors.R15,
      R16: b11CropResistors.R16,
      R17: b11CropResistors.R17,
    },
    topology_candidates: candidateRows,
    local_dc_checkpoints: {
      id: "B11_TS1_TS2_LOCAL_PASSIVE_KCL",
      status: STATUS.CANDIDATE,
      currents_amp: {
        iR10: passive.iR10,
        iR11: passive.iR11,
        n215ResidualFromPrintedVoltagesAmp: passive.n215ResidualFromPrintedVoltagesAmp,
        iR15: passive.iR15,
        iR13: passive.iR13,
        iR16: passive.iR16,
        n9PassiveDemandAmp: passive.n9PassiveDemandAmp,
        n145MinusN9PassiveDemandAmp: passive.n145MinusN9PassiveDemandAmp,
        iR12: passive.iR12,
        iR9: passive.iR9,
        n05DemandWithoutR17Amp: passive.n05DemandWithoutR17Amp,
        n05DeficitAfterR13Amp: passive.n05DeficitAfterR13Amp,
      },
      boundary:
        "These currents are local passive consistency checks around B11 Ts1/Ts2. They do not identify B/C/E pins and do not close the active regulator.",
    },
    unresolved_before_active_b11: [
      "Full B11 component graph around detector, regulator, S6, S7, C11 and command output.",
      "B/C/E orientation for all B11 active devices.",
      "Resolution of value/path divergences in prior notes before promoting R15/R16/R13 to closed global netlist stamps.",
      "Connection between B11 detector storage/C11 and the real B6 CMD drive node.",
    ],
  };
}

function buildB11LocalTopologyProofLedger() {
  const r = b11CropResistors;
  const v = b11CropVoltages;
  const iR15 = (v.V24 - v.N145) / r.R15;
  const iR16 = (v.N9 - v.REF) / r.R16;
  const iR13 = (v.N9 - v.N05) / r.R13;
  const residual = iR15 - iR16 - iR13;
  const residualRatio = residual / iR15;

  return {
    id: "B11_LOCAL_TOPOLOGY_PROOF_LEDGER_STEP1",
    status: STATUS.SCHEMATIC_CANDIDATE_PARTIAL,
    mna_action: "topology_ledger_only_not_stamped",
    scope:
      "Local B11 topology proof ledger around N145/N10/N9/N05/C5/R13/R16/R17/S6/S7/C11/CMD.",
    canonical_value_reading_ohm: {
      R15: r.R15,
      R16: r.R16,
      R13: r.R13,
    },
    rejected_value_readings: [
      {
        component: "R15",
        rejected_value_ohm: 12000,
        accepted_value_ohm: r.R15,
        reason: "German decimal-comma scan reading is 1,2 kOhm, not 12 kOhm.",
      },
      {
        component: "R16",
        rejected_value_ohm: 12000,
        accepted_value_ohm: r.R16,
        reason: "German decimal-comma scan reading is 1,2 kOhm, not 12 kOhm.",
      },
    ],
    printed_voltages_volt: {
      V24: v.V24,
      N145: v.N145,
      N9: v.N9,
      N05: v.N05,
      N10: v.N10,
    },
    local_kcl_checkpoint: {
      id: "B11_N145_N9_N05_R15_R16_R13_KCL",
      status: STATUS.STRONG_CANDIDATE,
      closure_status: "not_closed_by_R15_R16_R13_alone",
      iR15_from_V24_to_N145_amp: iR15,
      iR16_from_N9_to_reference_amp: iR16,
      iR13_from_N9_to_N05_amp: iR13,
      iR16_plus_iR13_amp: iR16 + iR13,
      residual_iR15_minus_iR16_minus_iR13_amp: residual,
      residual_ratio_of_iR15: residualRatio,
      verdict:
        "Coherent order of magnitude, but not FERME_SCHEMA. PDF evidence confirms the local passive endpoints; about 0.378 mA remains to explain through Ts2 active current and adjacent active routes.",
      promotion_blocker:
        "Do not use IR15 ~= IR16 + IR13 as a closed proof: PDF evidence confirms R13/R16/Ts2/C8/C9 local endpoints, but Ts2 B/C/E and the active side-current route remain unproven.",
    },
    route_proof_table: [
      {
        node: "B11_N145",
        components_touching_candidate: ["R15", "Ts2 terminal", "R14/C6", "C8"],
        confidence: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        must_prove: "Ts2 B/C/E orientation and the active current path that accounts for the 0.378 mA residual.",
      },
      {
        node: "B11_N10",
        components_touching_candidate: ["R11", "Ts1 terminal", "R14/C6"],
        confidence: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        must_prove: "Ts1 B/C/E orientation and any hidden/intermediate base route before active stamping.",
      },
      {
        node: "B11_N9",
        components_touching_candidate: ["R16", "R13", "Ts2 terminal", "C9"],
        confidence: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        must_prove: "Ts2 B/C/E orientation and the active source/sink relation at N9.",
      },
      {
        node: "B11_N05",
        components_touching_candidate: ["R12", "R9", "R13", "Ts1 terminal", "C5"],
        confidence: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        must_prove: "Ts1 B/C/E orientation and the active branch that supplies the remaining N05 current.",
      },
      {
        node: "B11_C5_ROUTE",
        components_touching_candidate: ["C5", "R7/R8 branch", "N05?"],
        confidence: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        must_prove: "Transient stamp and charge model; C5 is visually a series dynamic path and remains open in DC.",
      },
      {
        node: "B11_R17_ROUTE",
        components_touching_candidate: ["R17", "C10?", "N4?"],
        confidence: STATUS.CANDIDATE,
        must_prove: "R17 must not be silently tied to N05 unless the PDF route proves it.",
      },
      {
        node: "B11_S6_S7_C11_CMD",
        components_touching_candidate: ["S6", "S7", "C11", "CMD"],
        confidence: "MODE_DEPENDENT_CANDIDATE_SWITCH_NETWORK",
        must_prove: "Contact truth table and C11/CMD route per mode before any single active B11 command path is promoted.",
      },
    ],
    priority_route_proofs: [
      "R13 endpoints are now PDF-confirmed as N9-to-N05 for topology evidence only.",
      "R16 endpoints are now PDF-confirmed as N9-to-reference for topology evidence only.",
      "R17 endpoints: keep N4-to-reference as the current candidate; do not reuse old N05 assumptions.",
      "Ts2 terminal graph around N145/N9/C8/C9 is PDF-confirmed; B/C/E assignment remains guarded.",
      "Ts1 local graph around N10/N05/C5 is PDF-confirmed only as terminal topology; B/C/E assignment remains guarded.",
      "S6/S7/C11/CMD mode routes before replacing the Thevenin command source.",
    ],
    boundary:
      "This is a topology proof ledger, not a simulation. It encodes corrected values and KCL evidence while blocking active B11 promotion.",
  };
}

function buildB11PdfTopologyEvidenceLedger() {
  return {
    id: "B11_PDF_TOPOLOGY_EVIDENCE_STEP1",
    status: STATUS.SCHEMATIC_CANDIDATE_PARTIAL,
    source_pdf: "Siemens_U273_Limiter.pdf",
    extracted_image: "results/pdf_evidence/pdf_image_obj56_3289x2304_photometric0.png",
    extractor: "solver/extract_pdf_ccitt_images.js",
    mna_action: "pdf_evidence_only_not_stamped",
    active_simulation_allowed: false,
    evidence_crops: {
      ts2_r13_r16_c8_c9:
        "results/pdf_evidence/crop_b11_ts2_r15_r16_c8_c9_close.png",
      c5_s6:
        "results/pdf_evidence/crop_b11_c5_s6_close.png",
      c11_detector_rectifier:
        "results/pdf_evidence/crop_b11_c11_detector_cmd_area.png",
      detector_command_r31_d1_d2:
        "results/pdf_evidence/crop_b11_c11_detector_cmd_area.png",
      s7_detector_boundary:
        "results/pdf_evidence/crop_b11_s7_c11_bridge_boundary.png",
      s6_to_b6_boundary_cmd_trace:
        "results/pdf_evidence/crop_b11_s6_to_b6_boundary_cmd_trace.png",
      s6_right_output_r1_r2_trace:
        "results/pdf_evidence/crop_b11_s6_right_output_r1_r2_trace.png",
    },
    proof_rows: [
      {
        component: "R13",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        value_ohm: b11CropResistors.R13,
        visual_route: "B11_N9 -> R13 220k -> B11_N05_Ts1_LOW",
        dc_stamp_candidate: "g13 between B11_N9 and B11_N05",
        evidence_crop: "crop_b11_ts2_r15_r16_c8_c9_close.png",
        promotion_limit:
          "Passive endpoint proof only; does not close the active Ts1/Ts2 current path.",
      },
      {
        component: "R16",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        value_ohm: b11CropResistors.R16,
        visual_route: "B11_N9 -> R16 1.2k -> B11_REF",
        dc_stamp_candidate: "g16 between B11_N9 and B11_REF",
        evidence_crop: "crop_b11_ts2_r15_r16_c8_c9_close.png",
        promotion_limit:
          "Endpoint proof only; old N145->N9 R16 reading remains rejected.",
      },
      {
        component: "Ts2",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        device: "SST117",
        terminal_graph: {
          upper_terminal: "B11_N145 shared with R15 bottom, R14 left/right chain and C8 right",
          lower_terminal: "B11_N9 shared with R16 top, R13 left and C9 right",
          control_terminal_candidate: "route into the R14/C6/N10/Ts1 local network",
        },
        pinout_status: "BCE_UNASSIGNED_GUARDED",
        evidence_crop: "crop_b11_ts2_r15_r16_c8_c9_close.png",
        promotion_limit:
          "Terminal graph is visual evidence; B/C/E names and Ebers-Moll stamps remain forbidden until an active hypothesis passes VBE/VCE/KCL.",
      },
      {
        component: "C8",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        value_farads: 25e-6,
        visual_route: "B11_NDRV_C78_CANDIDATE -> C8 25u -> B11_N145",
        dc_policy: "open_in_dc",
        evidence_crop: "crop_b11_ts2_r15_r16_c8_c9_close.png",
        endpoint_coupling_limit:
          "C7/C8 common driver is a strong candidate only. Do not merge NDRV_C78 with NDRV_C9 without visible continuous conductor proof.",
        promotion_limit:
          "Dynamic endpoint proof only; no AC/transient stamp is promoted in the official netlist.",
      },
      {
        component: "C9",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        value_farads: 25e-6,
        visual_route: "B11_NDRV_C9_CANDIDATE -> C9 25u -> B11_N9",
        dc_policy: "open_in_dc",
        evidence_crop: "crop_b11_ts2_r15_r16_c8_c9_close.png",
        endpoint_coupling_limit:
          "C9 remains a separate driver-side candidate from NDRV_C78 until a visible continuous conductor proof says otherwise.",
        promotion_limit:
          "Dynamic endpoint proof only; no AC/transient stamp is promoted in the official netlist.",
      },
      {
        component: "C7",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        value_farads: 25e-6,
        visual_route: "B11_NDRV_C78_CANDIDATE -> C7 25u -> B11_N215",
        dc_policy: "open_in_dc",
        evidence_crop: "crop_b11_c5_c11_cmd_wide.png",
        endpoint_coupling_limit:
          "C7/C8 common driver is a strong candidate only. Do not merge NDRV_C78 with NDRV_C9 without visible continuous conductor proof.",
        promotion_limit:
          "Dynamic endpoint proof only; no AC/transient stamp is promoted in the official netlist.",
      },
      {
        component: "C5",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        value_farads: b11CropCapacitors.C5,
        visual_route: "B11_N05_Ts1_LOW -> C5 100u -> B11_N150_R7_TOP -> R7/R8/S6",
        dc_policy: "open_in_dc_between_N05_and_R7_R8_S6",
        evidence_crop: "crop_b11_c5_s6_close.png",
        promotion_limit:
          "C5 proves R7/R8/S6 are not direct DC load at N05; transient/attack-release still needs an implicit capacitor stamp.",
      },
      {
        component: "C11",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        value_farads: 500e-6,
        visual_route: "B11_NRECT_FILTERED_AFTER_R30_SI -> C11 500u -> B11_RECT_RETURN_22_24",
        role: "rectifier_filter_storage_not_direct_cmd",
        evidence_crop: "crop_b11_c11_detector_cmd_area.png",
        promotion_limit:
          "C11 is visually tied to the rectifier/fused storage area. The direct detector-to-B6-CMD transfer function is still not visually closed.",
      },
      {
        component: "R31",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        value_ohm: 51e3,
        visual_route: "B11_ND2_BOT_RAW -> R31 51k -> B11_NCMD_LOCAL",
        dc_stamp_candidate: "g31 between B11_ND2_BOT_RAW and B11_NCMD_LOCAL",
        evidence_crop: "crop_b11_c11_detector_cmd_area.png",
        promotion_limit:
          "Detector/control endpoint proof only; B11_NCMD_LOCAL is not the software B6 CMD node and must not replace the Thevenin command path.",
      },
      {
        component: "D2_SSD55",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        model_candidate: "SSD55_candidate",
        visual_route: "B11_N20_D2_TOP -> D2 SSD55 -> B11_ND2_BOT_RAW",
        polarity_status: "graphical_endpoint_only_guarded",
        n_top: "B11_N20_D2_TOP",
        n_bar: "B11_ND2_BOT_RAW",
        anode_candidate: "B11_N20_D2_TOP",
        cathode_candidate: "B11_ND2_BOT_RAW",
        spice_polarity_promoted: false,
        evidence_crop: "crop_b11_c11_detector_cmd_area.png",
        promotion_limit:
          "Diode endpoint proof only; Shockley/RS parameters and final polarity stamp remain guarded.",
      },
      {
        component: "D1_ZL10",
        status: STATUS.PDF_VISUAL_CONFIRMED_CANDIDATE,
        model_candidate: "ZL10_candidate",
        visual_route: "B11_NCMD_LOCAL -> D1 ZL10 -> B11_NZENER_OUT_18_19",
        polarity_status: "graphical_endpoint_only_guarded",
        zener_bar_candidate: "B11_NCMD_LOCAL",
        other_terminal: "B11_NZENER_OUT_18_19",
        anode_candidate: "B11_NZENER_OUT_18_19",
        cathode_candidate: "B11_NCMD_LOCAL",
        bv_volt_candidate: 10,
        functional_role_from_pdf_text: "limiter_threshold_zener",
        s7_bypass_in_compressor_functional_from_pdf_text: true,
        model_parameters_candidate: {
          BV: 10,
          IBV: 1e-3,
          N: 2,
          RS: 5,
          CJO: 0,
        },
        spice_polarity_promoted: false,
        evidence_crop: "crop_b11_c11_detector_cmd_area.png",
        promotion_limit:
          "Zener endpoint and functional limiter-threshold role are captured, but polarity stamp and S7 17/18/19 truth table remain guarded.",
      },
      {
        component: "CMD_ROUTE",
        status: "MODE_DEPENDENT_CANDIDATE_SWITCH_NETWORK",
        visual_route:
          "B11 active output -> C5/R7/R8/S6 visible network -> S6 right contacts -> R1/R2 boundary conductor candidate -> B6 bridge command remains represented by Thevenin port",
        boundary_trace_confirmed: true,
        c11_direct_to_cmd_proven: false,
        replace_thevenin_b11_s6_s7_cmd: false,
        evidence_crop: "crop_b11_s6_to_b6_boundary_cmd_trace.png",
        promotion_limit:
          "The PDF confirms a visible S6/R1/R2 boundary conductor candidate, but not a complete active output impedance or all mode contacts. Do not replace B11_S6_S7.VB11_S6_S7 + RB11_S6_S7_CMD until S6/S7 truth tables and active B11 output impedance are closed.",
      },
    ],
    closure_verdict: {
      passive_routes_proven:
        ["R13", "R16", "Ts2 terminal graph", "C8", "C9", "C7", "C5", "C11", "R31", "D2_SSD55", "D1_ZL10"],
      still_guarded:
        ["Ts1/Ts2 B/C/E", "active current closure", "S6/S7 full truth tables", "diode model parameters and polarities", "C7/C8/C9 driver-side common-node hypothesis", "C11/detector-to-CMD transfer", "Thevenin replacement"],
      bjt_pinout_promotable: false,
      active_b11_promotable: false,
      replace_thevenin_b11_s6_s7_cmd: false,
    },
    boundary:
      "PDF visual proof closes several local routes but remains topology evidence only. It must not stamp B11, assign BJT pins, or replace the guarded realtime/Thevenin command path.",
  };
}

function buildB11PdfTextFunctionalEvidence() {
  return {
    id: "B11_PDF_TEXT_FUNCTIONAL_EVIDENCE_STEP1",
    status: STATUS.PDF_TEXT_FUNCTIONAL_CONFIRMED,
    source_pdf: "Siemens_U273_Limiter.pdf",
    source_text_file: "C:/Users/user/.claude/plans/Analyse texte du pdf U273.txt",
    source_encoding: "UTF-8",
    authority:
      "PDF text functional evidence only. It can constrain mode behavior and validation targets, but it cannot promote contact truth tables, diode polarity, BJT pins, or MNA stamps.",
    mna_action: "functional_evidence_only_not_stamped",
    active_simulation_allowed: false,
    executable_netlist_effect: "none",
    confirmed_functional_facts: [
      {
        id: "B11_REGELVERSTAERKER_FEEDBACK_FROM_B6_OUTPUT",
        fact:
          "B11 is the regulation amplifier fed from the B6 signal amplifier output, so the control path is feedback, not arbitrary feed-forward.",
        modeling_consequence:
          "Future active closure must derive B11 drive from the B6 output node and cannot keep a free detector drive as proof.",
      },
      {
        id: "B11_RECTIFIER_BEFORE_DC_CONTROL",
        fact:
          "The B11 output is followed by a silicon rectifier that produces the DC control voltage.",
        modeling_consequence:
          "The detector path must be part of the offline active reference before C11/CMD dynamics can be promoted.",
      },
      {
        id: "COMPRESSOR_DIRECT_CONTROL_TO_BRIDGE",
        fact:
          "In compressor mode, the rectified DC control voltage is sent directly to the gain-control diode bridge.",
        modeling_consequence:
          "A compressor-mode candidate may bypass the limiter zener, but only after S7 contacts are proven.",
      },
      {
        id: "LIMITER_ZL10_THRESHOLD",
        fact:
          "In limiter mode, a ZL10 zener is inserted so regulation starts close to nominal output.",
        modeling_consequence:
          "D1_ZL10 has a functionally confirmed limiter-threshold role with BV around 10 V as a candidate model parameter.",
      },
      {
        id: "S7_BYPASSES_ZL10_IN_COMPRESSOR",
        fact:
          "The text says the zener is short-circuited by a switch in compressor mode.",
        modeling_consequence:
          "S7 has a functionally confirmed D1-bypass role candidate, but the exact 17/18/19 contact truth table still comes from the schematic.",
      },
      {
        id: "S6_PREEMPHASIS_SWITCHING",
        fact:
          "The regulation-amplifier input divider can bypass a resistance with a capacitor for FM pre-emphasis.",
        modeling_consequence:
          "S6/S7 must be treated as a mode-dependent pre-emphasis and command-routing switch matrix, not a single RC shortcut.",
      },
      {
        id: "AUDIO_GAIN_BRIDGE_GE_SI_BRANCH",
        fact:
          "Each audio gain-control bridge branch uses one germanium diode plus one silicon diode in series.",
        modeling_consequence:
          "The B6 gain bridge should use the Siemens empirical branch law or a Ge+Si equivalent, not a generic four-silicon-diode shortcut.",
      },
    ],
    mode_logic_candidates: {
      limiter: {
        d1_zl10: "inserted_as_threshold_zener",
        functional_path_candidate:
          "B11 rectifier -> D1 ZL10 threshold -> gain-control diode bridge",
        contact_truth_table_promoted: false,
        contact_proof_required: "prove which S7 contact state inserts D1_ZL10 instead of bypassing it",
      },
      compressor: {
        d1_zl10: "bypassed_by_switch_candidate",
        functional_path_candidate:
          "B11 rectifier -> S7 bypass around D1_ZL10 -> gain-control diode bridge",
        contact_truth_table_promoted: false,
        contact_proof_required: "prove the S7 short across D1_ZL10 on the page-1 contact graph",
      },
    },
    component_constraints: [
      {
        component: "D1_ZL10",
        functional_role: "limiter_threshold_zener",
        bv_volt_candidate: 10,
        s7_bypass_in_compressor_functional: true,
        spice_polarity_promoted: false,
        contact_truth_table_promoted: false,
        promotion_limit:
          "The text confirms D1 function, not its SPICE polarity or the exact S7 contact pair.",
      },
      {
        component: "S7",
        functional_role: "compressor_bypass_of_D1_ZL10_and_limiter_insertion_of_D1_ZL10",
        promoted_labels_forbidden: ["S7 = compressor", "S7 = limiter"],
        required_contact_proof:
          "per mechanical position: contact_closed/contact_open/left_node/right_node/effect_on_CMD_D1_D2",
      },
      {
        component: "S6",
        functional_role: "preemphasis_insertion_or_bypass_in_regulation_amplifier_input_network",
        promoted_truth_table: false,
        required_contact_proof:
          "per mechanical position: prove which R/C branches are inserted, bypassed, or isolated",
      },
      {
        component: "C11",
        functional_role: "detector_storage_release_candidate",
        direct_cmd_connection_promoted: false,
        dynamic_targets_source:
          "PDF text attack/release values constrain future transient validation, but do not prove C11-to-CMD topology.",
      },
    ],
    empirical_diode_branch_law: {
      scope: "B6 audio gain-control diode bridge branch, not D1_ZL10 or D2_SSD55 detector diodes",
      branch_composition: "one germanium diode plus one silicon diode in series",
      valid_current_range_uA: [2, 500],
      voltage_formula: "UD_mV = 308 * ID_uA^0.16",
      current_formula: "ID_uA = 2.85e-16 * UD_mV^6.25",
      differential_resistance_formula: "R_kohm = 48.3 * ID_uA^-0.84",
      bridge_signal_target_mV: {
        min: 10,
        nominal: 25,
        max: 50,
      },
      promotion_limit:
        "This law can validate the gain-control bridge model, but it is not a detector diode model and does not close B11.",
    },
    dynamic_plausibility_targets: {
      attack_ms: {
        limiter: 0.5,
        compressor: 1.0,
      },
      release_s: {
        min: 0.5,
        max: 1.5,
      },
      nominal_output_volt: 1.55,
      output_impedance_ohm: 30,
      load_ohm: 300,
      frequency_range_hz: [40, 15000],
    },
    not_confirmed_from_text: [
      "Complete S6/S7 contact truth table.",
      "Exact SPICE polarity of D1_ZL10 and D2_SSD55.",
      "Direct C11-to-B6-CMD connection.",
      "Whether C7/C8/C9 driver-side terminals share one conductor.",
      "B/C/E pinout for Ts1/Ts2/Ts3/Ts4 or any other active transistor.",
    ],
    activation_criterion_for_next_step: [
      "Prove visually which S7 contact bypasses D1_ZL10 in compressor mode.",
      "Prove visually which S6 contacts insert or bypass the pre-emphasis network.",
      "Run a guarded D1_ZL10 polarity experiment only after the S7 path is contact-proven.",
      "Keep the Siemens empirical diode law scoped to the audio gain bridge until detector diode models are separately proven.",
    ],
    closure_verdict: {
      d1_zl10_functional_role_confirmed: true,
      s7_d1_bypass_function_confirmed: true,
      s7_contact_truth_table_promoted: false,
      s6_preemphasis_function_confirmed: true,
      diode_bridge_empirical_law_confirmed_for_audio_bridge: true,
      active_b11_promotable: false,
      replace_thevenin_b11_s6_s7_cmd: false,
    },
    boundary:
      "The PDF text strengthens the functional model and future validation gates, but it does not activate B11, stamp D1/D2, assign BJT pins, or replace the guarded Thevenin command port.",
  };
}

function buildB11GptReponse2PromotionAudit() {
  return {
    id: "B11_GPT_REPONSE_2_2105_PROMOTION_AUDIT",
    status: STATUS.CANDIDATE,
    source_file: "C:/Users/user/.claude/plans/Gpt reponse 2 2105.txt",
    source_encoding: "UTF-8",
    authority:
      "External promotion proposal audit only. It can add cross-checks and explicit rejections, but it cannot promote topology, contacts, diode stamps, or active B11.",
    mna_action: "audit_only_not_stamped",
    active_simulation_allowed: false,
    executable_netlist_effect: "none",
    claims_accepted_as_guarded_constraints: [
      {
        claim: "D1_ZL10 anode=NZENER_OUT, cathode=NCMD, BV about 10 V",
        project_action:
          "Already recorded as a polarity/model candidate and as text-confirmed limiter-threshold function; spice_polarity_promoted remains false.",
      },
      {
        claim: "D2_SSD55 anode=N20_METER_TOP, cathode=NRECT_MINUS/ND2_BOT_RAW",
        project_action:
          "Already recorded as a polarity candidate; SSD55 model and polarity stamp remain guarded.",
      },
      {
        claim: "R31 is 51k between detector raw minus and local NCMD",
        project_action:
          "Already recorded as B11_ND2_BOT_RAW -> R31 -> B11_NCMD_LOCAL, outside the software B6 CMD boundary.",
      },
      {
        claim: "C11 must not appear in KCL(NCMD)",
        project_action:
          "Accepted as a promotion blocker: C11 remains not-direct-CMD evidence and is not connected to the software CMD node.",
      },
      {
        claim: "S7 bypasses D1 in compressor and leaves D1 active in limiter",
        project_action:
          "Accepted only as functional mode logic from the PDF text; S7 contact truth table remains unpromoted.",
      },
    ],
    claims_rejected_or_not_promotable: [
      {
        claim: "C7.right = +24V rail",
        project_verdict: "REJECT_FOR_STEP1",
        reason:
          "The local crop used by the current ledger shows C7 terminating on the printed 21.5 V node, not on the 24 V rail. Keep B11.PASSIVE.C7 as NDRV_C78_CANDIDATE -> B11_N215.",
        evidence_crop: "results/pdf_evidence/crop_b11_c5_c11_cmd_wide.png",
      },
      {
        claim: "C7/C8/C9 driver topology is resolved",
        project_verdict: "TOO_STRONG",
        reason:
          "C7/C8/C9 endpoints are useful, but the driver-side conductor grouping is still a guarded visual hypothesis. Do not merge or stamp the dynamic driver nodes from this file.",
      },
      {
        claim: "D1 and D2 polarity and stamps are promotable now",
        project_verdict: "TOO_STRONG",
        reason:
          "The graphical polarity candidates agree with the current ledger, but diode model parameters and S7 mode routing still need an explicit guarded experiment before stamping.",
      },
      {
        claim: "S6 minimum limiter/compressor truth table is resolved as 3->1 and 3->2",
        project_verdict: "TOO_STRONG",
        reason:
          "The project still requires per-position closed/open contacts, left/right nodes, and CMD/D1/D2 effects before S6 can drive active B11 routing.",
      },
      {
        claim: "B11 can move to partial_promoted_guarded official components",
        project_verdict: "REJECT_FOR_STEP1",
        reason:
          "No active B11 transistor pinout, no full S6/S7 truth table, no validated D1/D2 nonlinear experiment, and no real B11-to-B6 CMD KCL equation are available yet.",
      },
    ],
    canonical_project_decisions_after_audit: {
      c7_endpoint: "B11_NDRV_C78_CANDIDATE -> B11_N215",
      c8_endpoint: "B11_NDRV_C78_CANDIDATE -> B11_N145",
      c9_endpoint: "B11_NDRV_C9_CANDIDATE -> B11_N9",
      c11_cmd_policy: "not_direct_cmd_not_in_kcl_ncmd",
      d1_zl10_policy: "polarity_candidate_and_function_confirmed_but_not_stamped",
      d2_ssd55_policy: "polarity_candidate_but_not_stamped",
      s7_policy: "functional_bypass_logic_confirmed_contact_truth_table_unpromoted",
      s6_policy: "switch_matrix_candidate_only",
      b11_activation_status: "blocked_before_active_promotion",
    },
    next_experimental_gate_unlocked_by_this_audit: [
      "Add a guarded D1/D2 polarity experiment only after S7 contact routing is proven.",
      "Re-read the C7 endpoint against the full PDF crop before any dynamic driver-node promotion.",
      "Keep C11 excluded from NCMD KCL in all B11 candidate experiments.",
      "Use S7 limiter/compressor bypass logic as a test expectation, not as a contact-table stamp.",
    ],
    closure_verdict: {
      useful_crosscheck: true,
      contains_step1_conflict: true,
      c7_to_v24_rejected: true,
      active_b11_promotable: false,
      add_official_b11_components_now: false,
      replace_thevenin_b11_s6_s7_cmd: false,
    },
    boundary:
      "This audit absorbs the useful parts of Gpt reponse 2 2105 while blocking its premature activation recommendation.",
  };
}

function buildB11ScientificActivationResearchLedger() {
  return {
    id: "B11_SCIENTIFIC_ACTIVATION_RESEARCH_LEDGER",
    status: STATUS.CANDIDATE,
    research_date: "2026-05-21",
    mna_action: "research_requirements_only_not_stamped",
    active_simulation_allowed: false,
    executable_netlist_effect: "none",
    authority:
      "This ledger records source-backed modelling requirements for the next guarded activation step. It does not promote topology, diode polarity, BJT pins, or official B11 components.",
    source_policy: {
      acceptable_for_stamp_equations:
        "primary papers, simulator manuals, and manufacturer/component datasheets or scans",
      acceptable_for_component_identity:
        "model-specific museum/manufacturer literature can identify old parts when manufacturer datasheets are missing",
      not_acceptable_for_promotion:
        "forum substitutions, marketplace AI summaries, and unsourced equivalents can only create test hypotheses",
    },
    scientific_sources: [
      {
        id: "HO_RUEHLI_BRENNAN_MNA_1975",
        kind: "paper",
        url: "https://doi.org/10.1109/TCS.1975.1084079",
        finding:
          "Modified nodal analysis is the correct equation framework for circuits with voltage sources and current-defined branches.",
        project_consequence:
          "Keep B11 activation inside the existing MNA residual/Jacobian path rather than building an ad-hoc detector law.",
      },
      {
        id: "NGSPICE_DIODE_BJT_MODEL_REFERENCE",
        kind: "simulator_manual",
        url: "https://ngspice.sourceforge.io/docs/ngspice-manual.pdf",
        finding:
          "SPICE diode models use IS/N/RS plus optional reverse breakdown parameters BV and IBV; BJT models expose NPN/PNP device models with nonlinear DC and charge parameters.",
        project_consequence:
          "D1_ZL10 cannot be represented by the current forward-only Shockley diode. Add an optional zener breakdown branch before any D1 stamp experiment.",
      },
      {
        id: "HOLTERS_ZOLZER_NONLINEAR_STATE_SPACE_2015",
        kind: "paper",
        url:
          "https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Holters-Z%C3%B6lzer-2015-A-Generalized-Method-for-the-Derivation-of-Non-linear-State-space-Models-from-Circuit-Schematics.pdf",
        finding:
          "Nonlinear state-space models can be derived from schematics while separating linear state evolution from nonlinear device currents.",
        project_consequence:
          "Use this only after offline B11 MNA closure. It is a reduction path for realtime, not a substitute for proving B11 topology.",
      },
    ],
    required_stamps_before_b11_activation: [
      {
        device: "ordinary_diode",
        current_convention:
          "current leaves anode and enters cathode as I(Va - Vc); Jacobian is dI/dV stamped on anode/cathode nodes",
        current_project_status:
          "implemented_for_forward_shockley_u273_empirical_audio_bridge_and_guarded_zener_breakdown",
        missing_for_b11:
          "series resistance still needs a bounded implementation before detector/zener parameter fitting",
      },
      {
        device: "zener_diode",
        current_convention:
          "same two-terminal nonlinear stamp as diode, with an additional reverse-breakdown current for V(anode,cathode) below -BV",
        minimum_parameters:
          ["IS", "N", "RS", "BV", "IBV", "NBV or breakdownIdeality", "gmin"],
        current_project_status:
          "guarded reverse-breakdown diode capability exists; D1_ZL10 is still not stamped",
        activation_gate:
          "D1_ZL10 stamp experiments must sweep both graphical polarities and reject the one that violates S7 mode behavior and printed-node KCL.",
      },
      {
        device: "npn_bjt_dc",
        current_convention:
          "terminal currents are positive leaving collector/base/emitter; Ebers-Moll Jacobian is stamped against collector/base/emitter node voltages",
        current_project_status:
          "minimal NPN Ebers-Moll residual/Jacobian already exists",
        missing_for_b11:
          "B/C/E pin assignments, source-stepping tests, and hidden/intermediate route proof for Ts1/Ts2",
      },
      {
        device: "charge_and_capacitance",
        current_convention:
          "junction and storage capacitances must be implicit companion stamps tied to the nonlinear operating point",
        current_project_status:
          "linear capacitors are implicit; nonlinear diode/BJT capacitances are not implemented",
        activation_gate:
          "not required for first DC-only B11 activation, required before transient/audio claims",
      },
    ],
    component_data_ledger: [
      {
        component: "D1_ZL10",
        role_in_project: "B11 limiter-threshold zener candidate",
        source_url: "https://www.radiomuseum.org/tubes/tube_zl10.html",
        source_strength: "identity_and_nominal_voltage",
        source_facts: [
          "silicon reference regulator diode",
          "10 V nominal",
          "12.5 W listed on the component page",
        ],
        model_candidate: {
          type: "zener_diode",
          BV_volt: 10,
          IBV_amp: 1.0e-3,
          IS_amp: "sweep_or_fit",
          N: "sweep_or_fit",
          RS_ohm: "sweep_or_fit",
          CJO_farad: "0_for_DC_first",
        },
        data_gap:
          "The source gives identity/nominal breakdown but not a full SPICE model. IBV/IS/N/RS are candidate parameters until I-V fit or datasheet scan is found.",
        promotion_allowed_now: false,
      },
      {
        component: "D2_SSD55",
        role_in_project: "B11 silicon rectifier/detector diode candidate",
        source_url: "https://www.radiomuseum.org/tubes/tube_ssd55.html",
        source_strength: "weak_identity_only",
        source_facts: ["silicon diode is likely", "no data available on the component page"],
        model_candidate: {
          type: "silicon_shockley_with_rs",
          IS_amp: "bounded_sweep",
          N: "bounded_sweep",
          RS_ohm: "bounded_sweep",
          CJO_farad: "0_for_DC_first",
        },
        data_gap:
          "No reliable SSD55 electrical datasheet found. Treat existing makeSsd55Approximation as a hypothesis, not evidence.",
        promotion_allowed_now: false,
      },
      {
        component: "OA154Q",
        role_in_project: "B6 audio gain bridge germanium diode element, not D1/D2 detector",
        source_url: "https://www.radiomuseum.org/tubes/tube_oa154q.html",
        source_strength: "component_electrical_summary",
        source_facts: [
          "germanium point-contact diode quartet",
          "IF 6 mA at UF 1 V",
          "reverse leakage 10 uA at 10 V",
          "Imax 20 mA",
          "Umax 50 V",
          "Tj max 100 C",
        ],
        project_preferred_model:
          "Use the Siemens U273 empirical Ge+Si branch law for the audio bridge where available; do not copy this model to D1_ZL10 or D2_SSD55.",
        promotion_allowed_now: false,
      },
      {
        component: "BCY66",
        role_in_project: "small-signal silicon NPN candidate in U273 active stages",
        source_url: "https://www.alldatasheet.com/datasheet-pdf/pdf/44426/SIEMENS/BCY66.html",
        source_strength: "manufacturer_datasheet_scan_indexed",
        source_facts: ["Siemens NPN silicon planar transistor"],
        model_candidate: {
          type: "npn_ebers_moll_dc",
          betaForward: "bounded_fit_from_operating_point_or_datasheet_parse",
          betaReverse: 2,
          IS_amp: "bounded_fit",
          capacitances: "defer_to_version_1",
        },
        data_gap:
          "The datasheet exists but project has not parsed a source-backed beta/capacitance table into calibration bounds.",
        promotion_allowed_now: false,
      },
      {
        component: "SST117",
        role_in_project: "B11/B6 silicon NPN candidate",
        source_url: "https://www.radiomuseum.org/tubes/tube_sst117.html",
        source_strength: "identity_and_polarity_only",
        source_facts: ["NPN silicon planar transistor"],
        model_candidate: {
          type: "npn_ebers_moll_dc",
          betaForward: "wide_bounded_sweep",
          betaReverse: 2,
          IS_amp: "wide_bounded_sweep",
        },
        data_gap:
          "No manufacturer electrical datasheet found. Forum claims such as BSX45/2N2219A similarity remain hypothesis only.",
        promotion_allowed_now: false,
      },
      {
        component: "SST116",
        role_in_project: "B11/B6 silicon NPN candidate",
        source_url: "https://www.radiomuseum.org/tubes/tube_sst116.html",
        source_strength: "weak_identity_and_polarity",
        source_facts: ["NPN (?) silicon planar transistor"],
        model_candidate: {
          type: "npn_ebers_moll_dc_if_pinout_passes",
          betaForward: "wide_bounded_sweep",
          betaReverse: 2,
          IS_amp: "wide_bounded_sweep",
        },
        data_gap:
          "Polarity is not as strong as SST117. Require stronger route proof and operating-point rejection before any stamp.",
        promotion_allowed_now: false,
      },
      {
        component: "2N3054",
        role_in_project: "power NPN transistor listed for U273, not the immediate B11 Ts1/Ts2 blocker",
        source_url: "https://my.centralsemi.com/datasheets/2N3054.PDF",
        source_strength: "modern_manufacturer_datasheet",
        source_facts: [
          "silicon NPN power transistor",
          "TO-66 package",
          "VCEO class about 55 V in accessible datasheet summaries",
          "IC class about 4 A",
        ],
        model_candidate: {
          type: "npn_ebers_moll_dc_then_power_bjt_bounds",
          immediate_b11_use: false,
        },
        data_gap:
          "Useful for inventory completeness, but not a reason to relax Ts1/Ts2 pinout proof.",
        promotion_allowed_now: false,
      },
    ],
    activation_sequence_recommended_by_research: [
      {
        step: "A",
        title: "Use the guarded zener-capable DiodeModel in isolated tests",
        scope:
          "C++ model capability only, with D1_ZL10-like polarity sweeps. No B11 topology promotion.",
      },
      {
        step: "B",
        title: "Add a guarded component-model inventory",
        scope:
          "Expose source strength and data gaps for ZL10, SSD55, OA154Q, BCY66, SST117, SST116, and 2N3054.",
      },
      {
        step: "C",
        title: "Run isolated D1/D2 detector polarity experiments",
        scope:
          "Use S7 functional constraints as rejection rules, but keep B11_NCMD_LOCAL separate from B6 CMD.",
      },
      {
        step: "D",
        title: "Only then start B11 active DC experiment",
        scope:
          "Stamp Ts1/Ts2 Ebers-Moll only under an explicit experimental flag and only for pinout hypotheses that pass VBE/VCE/KCL.",
      },
    ],
    promotion_blockers_remaining_after_research: [
      "ZL10 has nominal BV evidence but no full I-V model.",
      "SSD55 has no trustworthy electrical parameter data.",
      "SST116/SST117 have weak or missing electrical datasheets.",
      "B11 Ts1/Ts2 B/C/E pinout is still unproven.",
      "S6/S7 contact truth tables still block the real CMD route.",
      "C11 remains rectifier/filter storage evidence, not direct CMD proof.",
    ],
    closure_verdict: {
      zener_model_required_before_d1_stamp: true,
      ebers_moll_dc_is_sufficient_for_first_active_dc_experiment: true,
      gummel_poon_required_now: false,
      component_data_complete_enough_for_official_activation: false,
      active_b11_promotable: false,
      replace_thevenin_b11_s6_s7_cmd: false,
    },
    boundary:
      "Research unlocks the next implementation step: zener-capable guarded experiments. It does not unlock official active B11 promotion.",
  };
}

function buildB11PdfActiveHypothesisConstraints() {
  const passive = b11CropPassiveKcl();
  return {
    id: "B11_PDF_ACTIVE_HYPOTHESIS_CONSTRAINTS_STEP1",
    status: STATUS.CANDIDATE,
    source: "B11_PDF_TOPOLOGY_EVIDENCE_STEP1 plus B11 passive printed-node KCL",
    mna_action: "hypothesis_constraints_only_not_stamped",
    allowed_use:
      "Use these rows to reject future Ts1/Ts2 active hypotheses that cannot meet sign and magnitude constraints.",
    forbidden_use:
      "Do not infer B/C/E, do not stamp Ebers-Moll, and do not replace the B11/S6/S7 Thevenin command source from these rows.",
    current_constraints: [
      {
        node: "B11_N145",
        printed_voltage_volt: b11CropVoltages.N145,
        required_active_current_into_node_amp: -passive.iR15,
        required_action: "sink_current_from_printed_node",
        magnitude_abs_amp: passive.iR15,
        likely_local_device_candidates: ["B11.Ts2"],
        evidence: "R15 injects current from V24 into N145; PDF shows Ts2 terminal, R14/C6 and C8 also touch this local region.",
        rejection_rule:
          "Reject a Ts2 hypothesis if it cannot remove about 7.92 mA from N145 while keeping VBE/VCE plausible.",
      },
      {
        node: "B11_N9",
        printed_voltage_volt: b11CropVoltages.N9,
        required_active_current_into_node_amp: passive.n9PassiveDemandAmp,
        required_action: "source_current_into_printed_node",
        magnitude_abs_amp: passive.n9PassiveDemandAmp,
        likely_local_device_candidates: ["B11.Ts2", "B11.Ts1"],
        evidence: "R16 and R13 draw current from N9; PDF shows Ts2 lower terminal, C9 and R13/R16 at this node.",
        rejection_rule:
          "Reject an isolated Ts2 hypothesis if it assumes R15/R16/R13 alone close KCL; N9 still needs about 7.54 mA sourced.",
      },
      {
        node: "B11_N05",
        printed_voltage_volt: b11CropVoltages.N05,
        required_active_current_into_node_amp: passive.n05DeficitAfterR13Amp,
        required_action: "source_current_into_printed_node",
        magnitude_abs_amp: passive.n05DeficitAfterR13Amp,
        likely_local_device_candidates: ["B11.Ts1"],
        evidence: "R12/R9 draw current from N05; R13 contributes only 38.6 uA, and C5/R7/R8/S6 is DC-open through C5.",
        rejection_rule:
          "Reject a Ts1 hypothesis if it cannot source about 4.14 mA into N05 without impossible beta or VBE/VCE.",
      },
    ],
    candidate_next_tests: [
      {
        device: "B11.Ts2",
        visual_terminal_graph: "N145 terminal, N9 terminal, control route into R14/C6/N10/Ts1 region",
        required_dc_behavior:
          "sink current at N145 and source current at N9, with remaining/base/side current reconciled through a proven control route",
        not_yet_allowed:
          "Do not label N145 collector or N9 emitter until the package pin mapping and base/control node are proven.",
      },
      {
        device: "B11.Ts1",
        visual_terminal_graph: "N10 terminal, N05 terminal, local connection to R13/C5/R12/R9 region",
        required_dc_behavior:
          "source the N05 residual current after R13 while remaining consistent with the N10/R11 balance",
        not_yet_allowed:
          "Do not label N10/N05 as collector/emitter until the hidden base/control route is proven.",
      },
    ],
    closure_verdict:
      "The PDF now gives enough route topology to build a constrained BJT-hypothesis evaluator, but not enough to accept a BJT pinout.",
    boundary:
      "These are active-hypothesis constraints, not active devices. They intentionally sit before Ebers-Moll stamping.",
  };
}

function buildB11RetranscriptionCrossCheck() {
  return {
    id: "B11_UTF8_RETRANSCRIPTION_CROSSCHECK_STEP1",
    status: STATUS.CANDIDATE,
    source_file: "C:/Users/user/.claude/plans/Retranscit gpt u273.txt",
    secondary_source_files: [
      "C:/Users/user/.claude/plans/gpt reponse 2105.txt",
      "C:/Users/user/.claude/plans/Gpt reponse 2 2105.txt",
    ],
    source_encoding: "UTF-8",
    authority:
      "Secondary checklist only. The retranscription can suggest KCL rows and routes, but PDF evidence and executable tests remain the promotion authority.",
    mna_action: "crosscheck_only_not_stamped",
    agrees_with_pdf_ledger: [
      {
        item: "R13",
        retranscription: "R13 = 220k between N9V and N05V",
        project_status: "already captured as B11_N9 -> R13 -> B11_N05_Ts1_LOW",
      },
      {
        item: "R16",
        retranscription: "R16 = 1.2k between N9V and reference",
        project_status: "already captured as B11_N9 -> R16 -> B11_REF",
      },
      {
        item: "R15",
        retranscription: "R15 = 1.2k between V24 and N14V3/N145",
        project_status: "already captured and used in the non-closed R15/R16/R13 KCL checkpoint",
      },
      {
        item: "Ts2",
        retranscription: "graphic terminals TOP=N14V3, RIGHT=NT2_R, BOTTOM=N9V",
        project_status: "compatible with the guarded terminal graph; B/C/E still unassigned",
      },
      {
        item: "C5",
        retranscription: "C5 = 100u from Ts1 right/output route toward R7",
        project_status: "compatible with the PDF ledger: C5 is a DC-open route into R7/R8/S6",
      },
      {
        item: "C8/C9",
        retranscription: "C8 and C9 are 25u dynamic branches into N145 and N9",
        project_status:
          "compatible with PDF endpoints; C7/C8 share NDRV_C78 as a guarded candidate while C9 stays separate",
      },
      {
        item: "C11",
        retranscription: "C11 = 500u in the detector/control region",
        project_status:
          "compatible in value, but the project keeps C11 as rectifier/filter storage, not direct CMD proof",
      },
      {
        item: "R31/D1/D2/NCMD",
        retranscription: "gpt reponse 2105 asserts D2 -> R31 51k -> NCMD -> D1 ZL10 -> 18/19/S7",
        project_status:
          "accepted as PDF visual endpoint evidence only; B11_NCMD_LOCAL is not the software B6 CMD node",
      },
      {
        item: "C7",
        retranscription: "C7 = 25u into the 21.5 V region with a driver-side candidate conductor",
        project_status:
          "accepted as a PDF visual endpoint candidate; C7/C8 common-driver hypothesis remains guarded",
      },
    ],
    useful_new_checklist_items: [
      {
        item: "B11.R31/D1/D2/NCMD",
        proposed_route:
          "D2 SSD55 into raw detector node, R31 = 51k toward NCMD, D1 ZL10 from NCMD toward points 18/19 and S7 logic",
        action:
          "Captured as detector endpoint evidence and disabled candidate components; still not a B6 CMD/Thevenin replacement.",
      },
      {
        item: "S6/S7 Vorentzerrung network",
        proposed_values:
          "R3=7.5k, R4=7.5k, R5=15k, R6=3.9k, C1=3n, C2=3n, C3=3.3n, C4=3n",
        action:
          "Use as a value checklist for the switch truth-table proof; do not reduce the switched network to one RC branch.",
      },
      {
        item: "C7",
        proposed_route:
          "C7 = 25u from a driver-side node into the N21.5V region",
        action:
          "Captured as a disabled dynamic candidate; prove or reject the C7/C8 common-driver hypothesis before dynamic B11 simulation.",
      },
      {
        item: "NDRV_A/NDRV_B/NDRV_C",
        proposed_rule:
          "driver-side capacitor terminals are internal excitation/coupling nodes, not ground",
        action:
          "Do not clamp them to reference. Keep C7/C8 as NDRV_C78 candidate and C9 as NDRV_C9 candidate until a continuous-conductor proof promotes or rejects the grouping.",
      },
    ],
    conflicts_or_unverified_items: [
      {
        item: "R2",
        retranscription_value: "1k trimmer",
        current_project_note: "existing visual inventory has a conflicting R2 visible-branch reading",
        verdict: "keep unresolved until the S6/R1/R2 boundary crop is re-read",
      },
      {
        item: "C11 to CMD",
        retranscription_value: "control-region candidate",
        current_project_note: "PDF ledger does not prove direct C11-to-B6-CMD transfer",
        verdict: "do not replace B11_S6_S7 Thevenin command port",
      },
      {
        item: "Ts1/Ts2 pin names",
        retranscription_value: "graphic terminals only",
        current_project_note: "matches project guard policy",
        verdict: "no Ebers-Moll BJT stamp from this retranscription",
      },
    ],
    rejected_bad_readings_called_out_by_transcription: [
      { component: "R7", rejected: "3k", candidate: "500 ohm trimmer" },
      { component: "R9", rejected: "5.6k", candidate: "56k" },
      { component: "R14", rejected: "1k", candidate: "2k" },
      { component: "R17", rejected: "750 ohm", candidate: "6.8k" },
      { component: "R31", rejected: "51 ohm", candidate: "51k" },
      { component: "C6", rejected: "10u", candidate: "1n" },
      { component: "C11", rejected: "25u", candidate: "500u" },
    ],
    next_pdf_proof_targets: [
      "B11.R31/D1/D2/NCMD electrical polarity and diode/zener model parameters",
      "B11.C7/C8 common-driver route promotion proof",
      "S6/S7 full switch truth table in limiter/compressor positions",
      "R1/R2/S6 boundary value and contact routing",
      "Whether NDRV_C78 and NDRV_C9 are separate nodes or a common conductor",
    ],
    closure_verdict:
      "The retranscription is useful for KCL preparation and for finding the next PDF crops. It does not promote B11, assign B/C/E, or replace the guarded Thevenin CMD path.",
  };
}

function buildB11PriorArtifactAudit() {
  const artifacts = [
    {
      artifact: "solver/b11_ts1_ts2_topology_constraints.js",
      status: STATUS.SUPERSEDED,
      issue_class: "dc_path_superseded_by_C5_open_reading",
      prior_assumption: "R7/R8 participates in the N05 DC demand bounds.",
      step1_canonical_reading:
        "C5 is a series capacitor from N05 toward the R7/R8 branch, so R7/R8 is DC-open from N05 in the passive clamp probe.",
      must_not_use_for: [
        "N05 DC demand proof",
        "B11 Ts1/Ts2 beta inference",
        "BJT pin promotion",
      ],
      allowed_use:
        "historical comparison only after its assumptions are explicitly reconciled with B11_PASSIVE_CANDIDATE_EXPERIMENTAL_PROBE",
    },
    {
      artifact: "solver/b11_ts2_r15_to_n05_path_solver.js",
      status: STATUS.CONFLICT,
      issue_class: "r16_r17_path_conflict",
      prior_assumption: "R16 is treated as N145->N9 and R17 as N05->reference.",
      step1_canonical_reading:
        "The current B11 visual inventory keeps R16 as N9->reference and R17 as N4->reference, not tied to N05.",
      must_not_use_for: [
        "Ts2 collector-current proof",
        "N05 path closure",
        "R7 fit",
        "BJT pin promotion",
      ],
      allowed_use:
        "only as a rejected/alternative hypothesis until the PDF route proof explicitly resolves R16/R17",
    },
    {
      artifact: "solver/b11_active_bjt_linearization_estimates.js",
      status: STATUS.SUPERSEDED,
      issue_class: "derived_from_pre_step1_current_bounds",
      prior_assumption:
        "Some q-point rows use local branch estimates and old N05 demand bounds as BJT small-signal seeds.",
      step1_canonical_reading:
        "Small-signal BJT estimates must now be regenerated from active_current_requirements after topology proof, not from mixed prior bounds.",
      must_not_use_for: [
        "gm/rpi acceptance",
        "B11 active model validation",
        "realtime surrogate derivation",
      ],
      allowed_use: "rough order-of-magnitude context only",
    },
    {
      artifact: "solver/b11_active_hybrid_pi_inventory.js",
      status: STATUS.SUPERSEDED,
      issue_class: "inherits_unproven_qpoint_rows",
      prior_assumption:
        "Hybrid-pi rows are generated before B11 terminal mapping and before the step-1 passive-current ledger.",
      step1_canonical_reading:
        "Hybrid-pi rows remain inventory-only until a unique B/C/E topology passes DC/KCL and later AC/transient gates.",
      must_not_use_for: [
        "terminal mapping",
        "active stamp generation",
        "promotion gate evidence",
      ],
      allowed_use: "not-stamped inventory reference only",
    },
  ];

  return {
    id: "B11_STEP1_PRIOR_ARTIFACT_AUDIT",
    status: STATUS.CANDIDATE,
    canonical_step1_sources: [
      "B11_VISUAL_INVENTORY_STEP1",
      "B11_PASSIVE_CANDIDATE_SUBCIRCUIT",
      "B11_PASSIVE_CANDIDATE_EXPERIMENTAL_PROBE when explicitly enabled",
    ],
    promotion_rule:
      "Any prior B11 script with a listed conflict must be reconciled before it can feed active topology, BJT pin inference, or calibration promotion.",
    artifact_count: artifacts.length,
    artifacts,
    boundary:
      "This audit prevents older exploratory B11 scripts from becoming silent evidence. It does not delete them and does not select an active topology.",
  };
}

const B11_BJT_PREFILTER_CRITERIA = Object.freeze({
  softVbeMinVolt: 0.45,
  softVbeMaxVolt: 0.85,
  strictVbeMinVolt: 0.55,
  strictVbeMaxVolt: 0.75,
  softVceMinVolt: 0.20,
  strictVceMinVolt: 0.40,
});

function b11DirectPrintedNodeSets() {
  return {
    Ts1: [
      { node: "B11_N215", voltage: b11CropVoltages.N215 },
      { node: "B11_N10", voltage: b11CropVoltages.N10 },
      { node: "B11_N9", voltage: b11CropVoltages.N9 },
      { node: "B11_N05", voltage: b11CropVoltages.N05 },
    ],
    Ts2: [
      { node: "B11_N145", voltage: b11CropVoltages.N145 },
      { node: "B11_N10", voltage: b11CropVoltages.N10 },
      { node: "B11_N9", voltage: b11CropVoltages.N9 },
      { node: "B11_N05", voltage: b11CropVoltages.N05 },
    ],
  };
}

function b11DirectPrintedNodeActiveCheck(kind, vc, vb, ve) {
  const npn = kind === "npn";
  const driveVolt = npn ? vb - ve : ve - vb;
  const outputVolt = npn ? vc - ve : ve - vc;
  const baseCollectorReverseVolt = npn ? vc - vb : vb - vc;
  const softDrive =
    driveVolt >= B11_BJT_PREFILTER_CRITERIA.softVbeMinVolt &&
    driveVolt <= B11_BJT_PREFILTER_CRITERIA.softVbeMaxVolt;
  const strictDrive =
    driveVolt >= B11_BJT_PREFILTER_CRITERIA.strictVbeMinVolt &&
    driveVolt <= B11_BJT_PREFILTER_CRITERIA.strictVbeMaxVolt;
  const softOutput = outputVolt >= B11_BJT_PREFILTER_CRITERIA.softVceMinVolt;
  const strictOutput = outputVolt >= B11_BJT_PREFILTER_CRITERIA.strictVceMinVolt;
  const reverseCollectorBase = baseCollectorReverseVolt > 0;
  const reasons = [];
  if (!softDrive) reasons.push(`drive ${driveVolt.toFixed(3)} V outside soft VBE window`);
  if (!softOutput) reasons.push(`output ${outputVolt.toFixed(3)} V below soft VCE window`);
  if (!reverseCollectorBase) reasons.push("collector/base ordering is not reverse-biased");

  return {
    drive_volt: driveVolt,
    output_volt: outputVolt,
    base_collector_reverse_volt: baseCollectorReverseVolt,
    soft_active_plausible: softDrive && softOutput && reverseCollectorBase,
    strict_active_plausible: strictDrive && strictOutput && reverseCollectorBase,
    rejection_reason: reasons.join("; ") || "voltage ordering plausible",
  };
}

function buildB11DirectPrintedNodePinPrefilter() {
  const rows = [];
  const nodeSets = b11DirectPrintedNodeSets();
  for (const [device, nodes] of Object.entries(nodeSets)) {
    for (const collector of nodes) {
      for (const base of nodes) {
        for (const emitter of nodes) {
          if (collector.node === base.node || collector.node === emitter.node || base.node === emitter.node) {
            continue;
          }
          for (const kind of ["npn", "pnp"]) {
            const check = b11DirectPrintedNodeActiveCheck(
              kind,
              collector.voltage,
              base.voltage,
              emitter.voltage
            );
            rows.push({
              device: `B11.${device}`,
              kind,
              collector: collector.node,
              base: base.node,
              emitter: emitter.node,
              collector_voltage_volt: collector.voltage,
              base_voltage_volt: base.voltage,
              emitter_voltage_volt: emitter.voltage,
              ...check,
              status: check.soft_active_plausible ? STATUS.CANDIDATE : "REJECTED_BY_DIRECT_PRINTED_NODE_VOLTAGE",
            });
          }
        }
      }
    }
  }

  const softRows = rows.filter((row) => row.soft_active_plausible);
  const strictRows = rows.filter((row) => row.strict_active_plausible);
  return {
    id: "B11_TS1_TS2_DIRECT_PRINTED_NODE_PIN_PREFILTER",
    status: STATUS.CANDIDATE,
    mna_action: "prefilter_only_not_stamped",
    criteria: B11_BJT_PREFILTER_CRITERIA,
    tested_device_count: Object.keys(nodeSets).length,
    tested_hypothesis_count: rows.length,
    soft_active_candidate_count: softRows.length,
    strict_active_candidate_count: strictRows.length,
    direct_printed_node_hypotheses: rows,
    conclusion:
      softRows.length === 0
        ? "No direct B/C/E permutation of the currently printed B11 Ts1/Ts2 nodes passes the soft voltage prefilter. B11 active closure needs hidden/intermediate route proof, not a direct printed-node guess."
        : "At least one direct printed-node permutation survives the soft voltage prefilter, but it remains non-stamped until KCL/AC/transient proof.",
    allowed_use:
      "Reject naive direct printed-node BJT pin assignments that violate VBE/VCE ordering.",
    forbidden_use:
      "Do not treat this as proof that the transistor is absent, and do not accept any BJT pinout from this prefilter alone.",
    boundary:
      "This prefilter uses printed DC voltages only. It does not use an active model, does not stamp BJTs, and does not replace route proof from the schematic/PDF.",
  };
}

function b11PassiveCandidateComponent(id, type, value, unit, nodes, fields = {}) {
  return {
    id,
    type,
    value: value === undefined ? null : finiteOrString(value),
    unit,
    nodes,
    status: STATUS.CANDIDATE,
    card: "B11",
    enabled_in_executable_netlist: false,
    mna_action: "candidate_disabled_not_stamped",
    source: "B11_VISUAL_INVENTORY_STEP1",
    boundary:
      "Passive B11 candidate component only. It is not part of the executable top-level components array and must not be stamped until topology proof promotes the subcircuit.",
    ...fields,
  };
}

function buildB11PassiveCandidateSubcircuit() {
  const r = b11CropResistors;
  const c = b11CropCapacitors;
  const passive = b11CropPassiveKcl();
  const candidateComponents = [
    b11PassiveCandidateComponent("B11.PASSIVE.R10", "resistor", r.R10, "ohm", { n1: "B11_V24", n2: "B11_N215" }, {
      function: "candidate supply/feed branch from printed 24 V node to 21.5 V node",
      dc_current_checkpoint_amp: passive.iR10,
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R11", "resistor", r.R11, "ohm", { n1: "B11_N215", n2: "B11_N10" }, {
      function: "candidate branch from printed 21.5 V node to 10 V node",
      dc_current_checkpoint_amp: passive.iR11,
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.C7", "capacitor", 25e-6, "farad", { n1: "B11_NDRV_C78_CANDIDATE", n2: "B11_N215" }, {
      function: "PDF-confirmed dynamic branch into the printed 21.5 V R10/R11 node",
      dc_policy: "open",
      route_limit: "C7/C8 common driver-side conductor is only a candidate; keep separate from NDRV_C9",
      evidence_crop: "results/pdf_evidence/crop_b11_c5_c11_cmd_wide.png",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R15", "resistor", r.R15, "ohm", { n1: "B11_V24", n2: "B11_N145" }, {
      function: "candidate feed into Ts1/Ts2 upper local node",
      dc_current_checkpoint_amp: passive.iR15,
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R14", "resistor", r.R14, "ohm", { n1: "B11_N145", n2: "B11_R14_C6_MID" }, {
      function: "candidate AC compensation branch series resistor",
      dc_policy: "blocked_by_series_C6",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.C6", "capacitor", c.C6, "farad", { n1: "B11_R14_C6_MID", n2: "B11_N10" }, {
      function: "candidate AC compensation branch series capacitor",
      dc_policy: "open",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.C8", "capacitor", 25e-6, "farad", { n1: "B11_NDRV_C78_CANDIDATE", n2: "B11_N145" }, {
      function: "PDF-confirmed dynamic branch into the printed 14.5 V Ts2/R15 node",
      dc_policy: "open",
      route_limit: "C7/C8 common driver-side conductor is only a candidate; keep separate from NDRV_C9",
      evidence_crop: "results/pdf_evidence/crop_b11_ts2_r15_r16_c8_c9_close.png",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.C9", "capacitor", 25e-6, "farad", { n1: "B11_NDRV_C9_CANDIDATE", n2: "B11_N9" }, {
      function: "PDF-confirmed dynamic branch into the printed 9 V Ts2/R16/R13 node",
      dc_policy: "open",
      route_limit: "C9 driver-side endpoint remains separate from NDRV_C78 until conductor proof",
      evidence_crop: "results/pdf_evidence/crop_b11_ts2_r15_r16_c8_c9_close.png",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R13", "resistor", r.R13, "ohm", { n1: "B11_N9", n2: "B11_N05" }, {
      function: "candidate high-value link between printed 9 V and 0.5 V nodes",
      dc_current_checkpoint_amp: passive.iR13,
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R16", "resistor", r.R16, "ohm", { n1: "B11_N9", n2: "B11_REF" }, {
      function: "candidate N9-to-reference branch",
      dc_current_checkpoint_amp: passive.iR16,
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R12", "resistor", r.R12, "ohm", { n1: "B11_N05", n2: "B11_REF" }, {
      function: "candidate N05 low-ohm reference branch",
      dc_current_checkpoint_amp: passive.iR12,
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R9", "resistor", r.R9, "ohm", { n1: "B11_N05", n2: "B11_REF" }, {
      function: "candidate N05 high-ohm reference branch",
      dc_current_checkpoint_amp: passive.iR9,
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.C5", "capacitor", c.C5, "farad", { n1: "B11_N05", n2: "B11_N150" }, {
      function: "candidate N05 coupling/storage path toward R7/R8 branch",
      dc_policy: "open",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R7_EFFECTIVE_MIN", "resistor", r.R7R8_MIN, "ohm", { n1: "B11_N150", n2: "B11_REF" }, {
      function: "candidate lower-bound effective R7/R8 branch after C5",
      dc_policy: "not_seen_from_N05_for_dc_because_C5_is_open",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R7_EFFECTIVE_MAX", "resistor", r.R7R8_MAX, "ohm", { n1: "B11_N150", n2: "B11_REF" }, {
      function: "candidate upper-bound effective R7/R8 branch after C5",
      dc_policy: "not_seen_from_N05_for_dc_because_C5_is_open",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R17", "resistor", r.R17, "ohm", { n1: "B11_N4", n2: "B11_REF" }, {
      function: "candidate R17 branch from printed N4 region to reference",
      topology_status: "not_connected_to_N05_in_current_visual_inventory",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.C11", "capacitor", 500e-6, "farad", { n1: "B11_NRECT_FILTERED_AFTER_R30_SI", n2: "B11_RECT_RETURN_22_24" }, {
      function: "PDF-confirmed rectifier/filter storage capacitor, not direct CMD proof",
      dc_policy: "open",
      evidence_crop: "results/pdf_evidence/crop_b11_c11_detector_cmd_area.png",
    }),
    b11PassiveCandidateComponent("B11.PASSIVE.R31", "resistor", 51e3, "ohm", { n1: "B11_ND2_BOT_RAW", n2: "B11_NCMD_LOCAL" }, {
      function: "PDF-confirmed detector/control resistor from D2 raw node to local NCMD",
      topology_status: "detector_endpoint_proven_cmd_boundary_guarded",
      evidence_crop: "results/pdf_evidence/crop_b11_c11_detector_cmd_area.png",
      promotion_limit: "B11_NCMD_LOCAL is not the software B6 CMD node.",
    }),
    b11PassiveCandidateComponent("B11.DIODE.D2_SSD55", "diode", "SSD55_candidate", "model", { n1: "B11_N20_D2_TOP", n2: "B11_ND2_BOT_RAW" }, {
      function: "PDF-confirmed detector diode endpoint candidate",
      polarity_status: "graphical_endpoint_only_guarded",
      anode_candidate: "B11_N20_D2_TOP",
      cathode_candidate: "B11_ND2_BOT_RAW",
      spice_polarity_promoted: false,
      evidence_crop: "results/pdf_evidence/crop_b11_c11_detector_cmd_area.png",
    }),
    b11PassiveCandidateComponent("B11.DIODE.D1_ZL10", "zener_diode", "ZL10_candidate", "model", { n1: "B11_NCMD_LOCAL", n2: "B11_NZENER_OUT_18_19" }, {
      function: "PDF-confirmed local command zener endpoint and text-confirmed limiter-threshold function candidate",
      polarity_status: "graphical_endpoint_only_guarded",
      anode_candidate: "B11_NZENER_OUT_18_19",
      cathode_candidate: "B11_NCMD_LOCAL",
      bv_volt_candidate: 10,
      functional_role_from_pdf_text: "limiter_threshold_zener",
      s7_bypass_in_compressor_functional_from_pdf_text: true,
      spice_polarity_promoted: false,
      evidence_crop: "results/pdf_evidence/crop_b11_c11_detector_cmd_area.png",
    }),
  ];

  return {
    id: "B11_PASSIVE_CANDIDATE_SUBCIRCUIT",
    status: STATUS.CANDIDATE,
    enabled_in_executable_netlist: false,
    stamp_policy: "disabled_by_default",
    promotion_required_before_stamp: [
      "PDF-wide B11 passive topology proof",
      "switch contact proof for S6/S7 in the selected debug position",
      "resolution of R15/R16/R13 value and path divergences across prior notes",
      "explicit connection from B11 detector/C11 storage to the B6 CMD drive node",
    ],
    candidate_component_count: candidateComponents.length,
    expected_executable_component_count_without_this_candidate: 88,
    local_dc_checkpoints: {
      n215ResidualFromPrintedVoltagesAmp: passive.n215ResidualFromPrintedVoltagesAmp,
      n9PassiveDemandAmp: passive.n9PassiveDemandAmp,
      n145MinusN9PassiveDemandAmp: passive.n145MinusN9PassiveDemandAmp,
      n05DemandWithoutR17Amp: passive.n05DemandWithoutR17Amp,
      n05DeficitAfterR13Amp: passive.n05DeficitAfterR13Amp,
    },
    candidate_components: candidateComponents,
    boundary:
      "This subcircuit is the next-step B11 passive candidate, deliberately stored outside the executable netlist. It is suitable for review and KCL bookkeeping only.",
  };
}

function b11PassiveCandidateExperimentMetadata() {
  return {
    id: "B11_PASSIVE_CANDIDATE_EXPERIMENTAL_PROBE",
    status: STATUS.CANDIDATE,
    enabled: false,
    enabled_by_default: false,
    explicit_enable_flag: "--enable-b11-passive-candidate-experiment",
    allowed_scope: "isolated_passive_kcl_probe",
    isolated_from_executable_netlist: true,
    modifies_official_components: false,
    expected_executable_component_count_without_this_candidate: 88,
    result: null,
    blocked_outputs: [
      "active B11 simulation",
      "BJT pin inference",
      "replacement of the B11/S6/S7 Thevenin command source",
      "promotion of switch contact candidates",
    ],
    boundary:
      "This probe is opt-in only. It may instantiate the B11 passive candidate in an isolated local DC clamp calculation, but it must not alter the official netlist or claim B11 active closure.",
  };
}

function b11PrintedVoltageClamps() {
  return {
    B11_V24: b11CropVoltages.V24,
    B11_N215: b11CropVoltages.N215,
    B11_N145: b11CropVoltages.N145,
    B11_N10: b11CropVoltages.N10,
    B11_N9: b11CropVoltages.N9,
    B11_N05: b11CropVoltages.N05,
  };
}

function b11NodeVoltageForExperiment(node, clamps) {
  if (!node || node === "0" || node === "GND" || node === "B11_REF") return 0.0;
  if (Object.prototype.hasOwnProperty.call(clamps, node)) return clamps[node];
  return null;
}

function b11PassiveExperimentDcStampDecision(componentInfo) {
  if (componentInfo.type === "capacitor") {
    return { stamped: false, reason: "capacitor_open_in_dc_probe" };
  }
  if (componentInfo.type === "diode" || componentInfo.type === "zener_diode") {
    return { stamped: false, reason: "diode_model_guarded_in_passive_probe" };
  }
  if (componentInfo.type !== "resistor") {
    return { stamped: false, reason: "non_resistor_excluded_from_passive_dc_probe" };
  }
  if (componentInfo.dc_policy === "blocked_by_series_C6") {
    return { stamped: false, reason: "series_R14_C6_branch_open_for_dc" };
  }
  if (componentInfo.dc_policy === "not_seen_from_N05_for_dc_because_C5_is_open") {
    return { stamped: false, reason: "behind_series_C5_open_for_dc" };
  }
  if (componentInfo.topology_status === "not_connected_to_N05_in_current_visual_inventory") {
    return { stamped: false, reason: "local_node_not_tied_to_current_printed_B11_probe" };
  }
  return { stamped: true, reason: "resistive_branch_between_clamped_printed_nodes" };
}

function b11ActiveRequirementHint(node) {
  const hints = {
    B11_N215: {
      local_area: "B11 Ts1/Ts2 upper local balance",
      possible_related_devices: ["B11.Ts1", "B11.Ts2"],
      evidence_limit:
        "Only a 0.1 mA passive residual between R10 and R11 is visible under printed-voltage clamps.",
    },
    B11_N145: {
      local_area: "B11 upper feed from R15",
      possible_related_devices: ["B11.Ts2"],
      evidence_limit:
        "R15 injects current into N145, but the active route leaving N145 is not yet pinned.",
    },
    B11_N10: {
      local_area: "B11 Ts1/Ts2 mid node",
      possible_related_devices: ["B11.Ts1", "B11.Ts2"],
      evidence_limit:
        "R11 injects current into N10. The sink path is active/topological, not identified here.",
    },
    B11_N9: {
      local_area: "B11 lower control node",
      possible_related_devices: ["B11.Ts1", "B11.Ts2"],
      evidence_limit:
        "R13 and R16 demand source current at N9 under the current visual candidate.",
    },
    B11_N05: {
      local_area: "B11 low-voltage reference/control node",
      possible_related_devices: ["B11.Ts1", "B11.Ts2"],
      evidence_limit:
        "R12/R9 demand exceeds the R13 inflow; the missing source path is active and unpinned.",
    },
  };
  return hints[node] ?? {
    local_area: "unclassified B11 printed node",
    possible_related_devices: [],
    evidence_limit: "No active-device interpretation is assigned.",
  };
}

function b11ActiveCurrentRequirementRows(nodeCurrentBalance) {
  return nodeCurrentBalance
    .filter((row) => row.node !== "B11_V24")
    .map((row) => {
      const requiredIntoNode = row.external_current_required_into_node_amp;
      const hint = b11ActiveRequirementHint(row.node);
      return {
        node: row.node,
        printed_voltage_volt: row.printed_voltage_volt,
        required_active_current_into_node_amp: requiredIntoNode,
        required_active_current_leaving_node_amp: -requiredIntoNode,
        magnitude_abs_amp: Math.abs(requiredIntoNode),
        required_action:
          requiredIntoNode >= 0
            ? "source_current_into_printed_node"
            : "sink_current_from_printed_node",
        status: STATUS.CANDIDATE,
        allowed_use: "future_hypothesis_rejection_constraint_only",
        forbidden_use: "do_not_accept_or_stamp_any_BJT_from_this_constraint",
        local_area: hint.local_area,
        possible_related_devices: hint.possible_related_devices,
        evidence_limit: hint.evidence_limit,
      };
    });
}

function b11ActiveCurrentRequirementSummary(activeRequirements, nodeCurrentBalance) {
  const supplyRow = nodeCurrentBalance.find((row) => row.node === "B11_V24");
  const sourceRequirements = activeRequirements.filter((row) => row.required_active_current_into_node_amp > 0);
  const sinkRequirements = activeRequirements.filter((row) => row.required_active_current_into_node_amp < 0);
  const largest = activeRequirements.reduce(
    (best, row) => (row.magnitude_abs_amp > best.magnitude_abs_amp ? row : best),
    { node: null, magnitude_abs_amp: 0 }
  );

  return {
    status: STATUS.CANDIDATE,
    constraint_count: activeRequirements.length,
    source_into_node_count: sourceRequirements.length,
    sink_from_node_count: sinkRequirements.length,
    largest_constraint_node: largest.node,
    largest_constraint_abs_amp: largest.magnitude_abs_amp,
    rail_supply_current_from_24v_amp: supplyRow ? supplyRow.passive_current_leaving_node_amp : null,
    required_next_proof:
      "Map these sign/magnitude constraints onto explicit diode/BJT terminal hypotheses, then reject hypotheses that cannot supply the required node current without impossible VBE/VCE or KCL.",
    boundary:
      "Current requirements are derived from passive printed-node clamps only. They are not transistor pin assignments and they do not replace the guarded B11 Thevenin source.",
  };
}

function buildB11PassiveCandidateExperiment(options = {}) {
  const metadata = b11PassiveCandidateExperimentMetadata();
  const enabled = options.enableB11PassiveCandidateExperiment === true;
  if (!enabled) return metadata;

  const subcircuit = buildB11PassiveCandidateSubcircuit();
  const clamps = b11PrintedVoltageClamps();
  const stamped = [];
  const excluded = [];

  for (const candidate of subcircuit.candidate_components) {
    const decision = b11PassiveExperimentDcStampDecision(candidate);
    if (!decision.stamped) {
      excluded.push({
        id: candidate.id,
        type: candidate.type,
        reason: decision.reason,
      });
      continue;
    }

    const n1 = candidate.nodes.n1;
    const n2 = candidate.nodes.n2;
    const v1 = b11NodeVoltageForExperiment(n1, clamps);
    const v2 = b11NodeVoltageForExperiment(n2, clamps);
    if (v1 === null || v2 === null) {
      excluded.push({
        id: candidate.id,
        type: candidate.type,
        reason: "endpoint_voltage_not_available_in_printed_B11_probe",
      });
      continue;
    }

    stamped.push({
      name: candidate.id,
      n1,
      n2,
      value: Number(candidate.value),
      source_component_id: candidate.id,
      voltage_n1_volt: v1,
      voltage_n2_volt: v2,
      current_from_n1_to_n2_amp: (v1 - v2) / Number(candidate.value),
    });
  }

  const netlist = {
    resistors: stamped.map((entry) => ({
      name: entry.name,
      n1: entry.n1,
      n2: entry.n2 === "B11_REF" ? "0" : entry.n2,
      value: entry.value,
    })),
    voltageSources: Object.entries(clamps).map(([node, value]) => ({
      name: `VCLAMP_${node}`,
      nPlus: node,
      nMinus: "0",
      value,
    })),
    diodes: [],
  };

  const solved = solveDc(netlist, {
    tolerance: 1.0e-12,
    residualTolerance: 1.0e-12,
    maxIterations: 4,
  });
  const solution = solutionObject(solved, netlist);

  const nodeBalances = new Map();
  function addBalance(node, value) {
    if (!node || node === "B11_REF" || node === "0" || node === "GND") return;
    nodeBalances.set(node, (nodeBalances.get(node) ?? 0) + value);
  }

  for (const branch of stamped) {
    addBalance(branch.n1, branch.current_from_n1_to_n2_amp);
    addBalance(branch.n2, -branch.current_from_n1_to_n2_amp);
  }

  const nodeCurrentBalance = Object.entries(clamps).map(([node, voltage]) => {
    const passiveCurrentLeaving = nodeBalances.get(node) ?? 0.0;
    const mnaSourceCurrent = solution[`I(VCLAMP_${node})`];
    return {
      node,
      printed_voltage_volt: voltage,
      passive_current_leaving_node_amp: passiveCurrentLeaving,
      mna_voltage_source_current_from_node_to_reference_amp: mnaSourceCurrent,
      external_current_required_into_node_amp: passiveCurrentLeaving,
      interpretation:
        passiveCurrentLeaving >= 0
          ? "positive means the omitted active circuitry or supply clamp must source this current into the printed node"
          : "negative means the omitted active circuitry must sink current from this printed node",
    };
  });
  const activeCurrentRequirements = b11ActiveCurrentRequirementRows(nodeCurrentBalance);
  const activeCurrentRequirementSummary = b11ActiveCurrentRequirementSummary(
    activeCurrentRequirements,
    nodeCurrentBalance
  );

  const branchById = Object.fromEntries(stamped.map((branch) => [branch.source_component_id, branch]));
  const passive = b11CropPassiveKcl();
  const crossChecks = {
    r10MinusR11Amp:
      branchById["B11.PASSIVE.R10"].current_from_n1_to_n2_amp -
      branchById["B11.PASSIVE.R11"].current_from_n1_to_n2_amp,
    r10MinusR11ReferenceAmp: passive.n215ResidualFromPrintedVoltagesAmp,
    n9PassiveDemandAmp:
      branchById["B11.PASSIVE.R13"].current_from_n1_to_n2_amp +
      branchById["B11.PASSIVE.R16"].current_from_n1_to_n2_amp,
    n9PassiveDemandReferenceAmp: passive.n9PassiveDemandAmp,
    n05DeficitAfterR13Amp:
      branchById["B11.PASSIVE.R12"].current_from_n1_to_n2_amp +
      branchById["B11.PASSIVE.R9"].current_from_n1_to_n2_amp -
      branchById["B11.PASSIVE.R13"].current_from_n1_to_n2_amp,
    n05DeficitAfterR13ReferenceAmp: passive.n05DeficitAfterR13Amp,
  };

  return {
    ...metadata,
    enabled: true,
    result: {
      subcircuit_id: subcircuit.id,
      executable_component_count_unchanged: true,
      official_component_count_reference: subcircuit.expected_executable_component_count_without_this_candidate,
      printed_voltage_clamps_volt: clamps,
      stamped_resistor_count: stamped.length,
      excluded_component_count: excluded.length,
      stamped_resistors: stamped,
      excluded_components: excluded,
      isolated_dc_solve: {
        converged: solved.converged,
        iterations: solved.iterations,
        residual_max_abs: solved.residual ? solved.residual.maxAbs : null,
        residual_rms: solved.residual ? solved.residual.rms : null,
      },
      node_current_balance_amp: nodeCurrentBalance,
      active_current_requirements: activeCurrentRequirements,
      active_current_requirement_summary: activeCurrentRequirementSummary,
      cross_checks: crossChecks,
      scientific_boundary:
        "The clamp currents are missing-current bookkeeping for a passive candidate only. They are not transistor operating points and must not be used as B/C/E proof.",
    },
  };
}

function component(id, type, value, unit, nodes, fields = {}) {
  return {
    id,
    type,
    value: value === undefined ? null : finiteOrString(value),
    unit,
    nodes,
    ...fields,
  };
}

function b6PassiveComponents(params) {
  return [
    component("B6.R5", "resistor", params.r5, "ohm", { n1: "VS", n2: "NX" }, {
      card: "B6",
      function: "audio input series path",
      status: STATUS.CLOSED,
      source_etape: "Etape 1",
    }),
    component("B6.C1", "capacitor", params.c1, "farad", { n1: "NX", n2: "NA" }, {
      card: "B6",
      function: "input coupling/bridge feed",
      status: STATUS.CLOSED,
      source_etape: "Etape 24",
    }),
    component("B6.C2", "capacitor", params.c2, "farad", { n1: "NA", n2: "NB" }, {
      card: "B6",
      function: "bridge feed coupling",
      status: STATUS.CLOSED,
      source_etape: "Etape 1",
    }),
    component("B6.R10", "resistor", params.r10, "ohm", { n1: "CMD", n2: "NB" }, {
      card: "B6",
      function: "command injection into bridge",
      status: STATUS.CLOSED,
      source_etape: "Etape 24",
    }),
    component("B6.R9", "resistor", params.r9, "ohm", { n1: "NB", n2: "0" }, {
      card: "B6",
      function: "bridge bias/reference",
      status: STATUS.CLOSED,
      source_etape: "Etape 1",
    }),
    component("B6.R7_effective", "resistor", params.r7, "ohm", { n1: "NL", n2: "NR" }, {
      card: "B6",
      function: "Abgl adjustable, reduced to effective two-terminal value",
      status: STATUS.PARAMETRIC,
      source_etape: "Etape 24",
    }),
    component("B6.R8_effective", "resistor", params.r8, "ohm", { n1: "N14", n2: "N15" }, {
      card: "B6",
      function: "Abgl adjustable, reduced to effective two-terminal value",
      status: STATUS.PARAMETRIC,
      source_etape: "Etape 24",
    }),
    component("B6.R6", "resistor", params.r6, "ohm", { n1: "N14", n2: "0" }, {
      card: "B6",
      function: "left bridge return",
      status: STATUS.CLOSED,
      source_etape: "Etape 1",
    }),
    component("B6.R11", "resistor", params.r11, "ohm", { n1: "N15", n2: "0" }, {
      card: "B6",
      function: "right bridge return",
      status: STATUS.CLOSED,
      source_etape: "Etape 1",
    }),
    component("B6.C3", "capacitor", params.c3, "farad", { n1: "N14", n2: "0" }, {
      card: "B6",
      function: "bridge/output time constant",
      status: STATUS.CLOSED,
      source_etape: "Etape 1",
    }),
    component("B6.C4_abgl", "capacitor", params.c4Abgl, "farad", { n1: "N14", n2: "0" }, {
      card: "B6",
      function: "adjustment capacitor",
      status: STATUS.PARAMETRIC,
      source_etape: "Etape 24",
    }),
    component("B6.C5", "capacitor", params.c5, "farad", { n1: "N14", n2: "N15" }, {
      card: "B6",
      function: "bridge/output coupling",
      status: STATUS.CLOSED,
      source_etape: "Etape 1",
    }),
    component("B6.C6_abgl", "capacitor", params.c6Abgl, "farad", { n1: "N15", n2: "0" }, {
      card: "B6",
      function: "adjustment capacitor",
      status: STATUS.PARAMETRIC,
      source_etape: "Etape 24",
    }),
    component("B6.C7", "capacitor", params.c7, "farad", { n1: "N15", n2: "0" }, {
      card: "B6",
      function: "bridge/output time constant",
      status: STATUS.CLOSED,
      source_etape: "Etape 1",
    }),
  ];
}

function b6DiodeComponents(dcNetlist) {
  return (dcNetlist.diodes || []).map((diode) =>
    component(`B6.${diode.name}`, "diode", null, null, {
      anode: diode.anode,
      cathode: diode.cathode,
    }, {
      card: "B6",
      function: "limiter bridge element",
      status: STATUS.PARTIAL,
      source_etape: "Etape 23/24",
      model: diode.model ?? "shockley_placeholder",
      nominal_type: diode.nominalType ?? diode.name,
      warning: "Bridge polarity is kept as current default until final photo proof.",
    })
  );
}

function commandPortComponents(options) {
  return [
    component("B11_S6_S7.VB11_S6_S7", "voltage_source", options.commandSourceVolt, "volt", {
      n_plus: "B11_DRV",
      n_minus: "0",
    }, {
      card: "B11/B6",
      function: "finite command source replacing old ideal VCMD",
      status: STATUS.PARAMETRIC,
      source_etape: "Etape 24",
    }),
    component("B11_S6_S7.RB11_S6_S7_CMD", "resistor", options.b6.commandSourceOhm, "ohm", {
      n1: "B11_DRV",
      n2: "CMD",
    }, {
      card: "B11/B6",
      function: "Thevenin output resistance of not-yet-closed active B11/S6/S7 path",
      status: STATUS.PARAMETRIC,
      source_etape: "Etape 24",
    }),
  ];
}

function b6NumericalComponents(params) {
  return [
    component("B6.GMIN_NL", "resistor", params.gminResistance, "ohm", { n1: "NL", n2: "0" }, {
      card: "B6",
      function: "MNA numerical stabilizer",
      status: STATUS.NUMERICAL,
      source_etape: "solver/b6_parametric.js",
    }),
    component("B6.GMIN_NR", "resistor", params.gminResistance, "ohm", { n1: "NR", n2: "0" }, {
      card: "B6",
      function: "MNA numerical stabilizer",
      status: STATUS.NUMERICAL,
      source_etape: "solver/b6_parametric.js",
    }),
  ];
}

function s6CoreComponents() {
  const v = s6CoreValues;
  return [
    component("B11.S6.R4", "resistor", v.r4, "ohm", { n1: "VL", n2: "VM" }, {
      card: "B11",
      function: "S6 Vorentzerrung core branch VL-VM",
      status: STATUS.PARTIAL,
      source_etape: "Etape 20/21",
    }),
    component("B11.S6.C2", "capacitor", v.c2, "farad", { n1: "VL", n2: "VM" }, {
      card: "B11",
      function: "S6 Vorentzerrung core branch VL-VM",
      status: STATUS.PARTIAL,
      source_etape: "Etape 20/21",
    }),
    component("B11.S6.R3", "resistor", v.r3, "ohm", { n1: "VM", n2: "VR" }, {
      card: "B11",
      function: "S6 Vorentzerrung core branch VM-VR",
      status: STATUS.PARTIAL,
      source_etape: "Etape 20/21",
    }),
    component("B11.S6.C1", "capacitor", v.c1, "farad", { n1: "VM", n2: "VR" }, {
      card: "B11",
      function: "S6 Vorentzerrung core branch VM-VR",
      status: STATUS.PARTIAL,
      source_etape: "Etape 20/21",
    }),
    component("B11.S6.R5", "resistor", v.r5, "ohm", { n1: "VL", n2: "VR" }, {
      card: "B11",
      function: "S6 Vorentzerrung optional bridge branch",
      status: STATUS.PARTIAL,
      source_etape: "Etape 20/21",
    }),
    component("B11.S6.C3", "capacitor", v.c3, "farad", { n1: "VL", n2: "VR" }, {
      card: "B11",
      function: "S6 Vorentzerrung optional bridge branch",
      status: STATUS.PARTIAL,
      source_etape: "Etape 20/21",
    }),
    component("B11.S6.R6", "resistor", v.r6, "ohm", { n1: "VM", n2: "S6_R6_C4_MID" }, {
      card: "B11",
      function: "S6 VM-to-ground series RC branch",
      status: STATUS.PARTIAL,
      source_etape: "Etape 20/21",
    }),
    component("B11.S6.C4", "capacitor", v.c4, "farad", { n1: "S6_R6_C4_MID", n2: "0" }, {
      card: "B11",
      function: "S6 VM-to-ground series RC branch",
      status: STATUS.PARTIAL,
      source_etape: "Etape 20/21",
    }),
  ];
}

function collectNodes(components) {
  const nodes = new Set();
  const add = (value) => {
    if (Array.isArray(value)) {
      for (const item of value) add(item);
      return;
    }
    if (value && typeof value === "object") {
      for (const item of Object.values(value)) add(item);
      return;
    }
    if (!value || value === "0" || String(value).toUpperCase() === "GND") return;
    nodes.add(value);
  };

  for (const entry of components) {
    add(entry.nodes || {});
  }

  return Array.from(nodes).sort().map((id) => ({ id }));
}

function b6BjtCandidates() {
  const hybridRows = b6TransistorHybridPiRows();
  const outputRows = b6OutputStageBounds();
  const candidates = hybridRows.map((row) => ({
    id: `B6.${row.transistor}.beta_${row.beta}`,
    board: "B6",
    transistor: row.transistor,
    status: "PARAMETRIQUE",
    stampMode: "NOT_STAMPED_BY_DEFAULT",
    terminalStatus: row.terminalStatus,
    type: row.type,
    collector: row.collector,
    base: row.base,
    emitter: row.emitter,
    qPointStatus: "conditional_from_printed_voltages_and_local_KCL",
    currentAmp: row.collectorCurrentAmp,
    beta: row.beta,
    earlyVoltageVolt: Infinity,
    roIncluded: false,
    gmSiemens: row.gmSiemens,
    rPiOhm: row.rPiOhm,
    emitterDynamicOhm: row.emitterDynamicOhm,
    baseCurrentAmp: row.baseCurrentAmp,
    sourceEvidence: row.evidence,
    notStampedReason:
      "Pinout/topology is not photo-proven. AC stamp is allowed only under an explicit named hypothesis.",
  }));

  return candidates.concat(outputRows.map((row) => ({
    id: `B6.${row.transistorGroup}.beta_${row.beta}.i_${row.idleCurrentMilliAmp}_mA`,
    board: "B6",
    transistor: row.transistorGroup,
    status: "BOUNDS_ONLY",
    stampMode: "NEVER_STAMP_UNTIL_TOPOLOGY_CONFIRMED",
    terminalStatus: row.terminalStatus,
    type: "unknown_output_pair",
    collector: null,
    base: null,
    emitter: null,
    qPointStatus: "current_bound_from_R36",
    currentAmp: row.idleCurrentAmp,
    beta: row.beta,
    earlyVoltageVolt: Infinity,
    roIncluded: false,
    gmSiemens: row.gmSiemens,
    rPiOhm: row.rPiOhm,
    emitterDynamicOhm: row.emitterDynamicOhm,
    baseCurrentAmp: row.baseCurrentMicroAmp * 1e-6,
    sourceEvidence: row.evidence,
    notStampedReason: row.limit,
  })));
}

function buildDcNetlist(options = {}) {
  const resolved = options.b6 ? options : resolveOptions(options);
  return buildB6ParametricDcNetlist(resolved.commandSourceVolt, resolved.b6);
}

function buildNetlist(options = {}) {
  const resolved = resolveOptions(options);
  const params = resolveB6Params(resolved.b6);
  const dcNetlist = buildDcNetlist(resolved);
  const b6Inventory = buildB6CompleteSchematicInventory(resolved.b6);
  const switchContactCandidates = buildSwitchContactCandidates(resolved);
  const b11S6S7SwitchMatrixCandidate = buildB11S6S7SwitchMatrixCandidate(resolved);
  const b11VisualInventory = buildB11VisualInventory();
  const b11LocalTopologyProofLedger = buildB11LocalTopologyProofLedger();
  const b11PdfTopologyEvidenceLedger = buildB11PdfTopologyEvidenceLedger();
  const b11PdfTextFunctionalEvidence = buildB11PdfTextFunctionalEvidence();
  const b11GptReponse2PromotionAudit = buildB11GptReponse2PromotionAudit();
  const b11ScientificActivationResearchLedger = buildB11ScientificActivationResearchLedger();
  const b11PdfActiveHypothesisConstraints = buildB11PdfActiveHypothesisConstraints();
  const b11RetranscriptionCrossCheck = buildB11RetranscriptionCrossCheck();
  const b11PriorArtifactAudit = buildB11PriorArtifactAudit();
  const b11DirectPrintedNodePinPrefilter = buildB11DirectPrintedNodePinPrefilter();
  const b11D1D2PolarityExperiment = buildB11D1D2PolarityExperiment(options);
  const b11PassiveCandidateSubcircuit = buildB11PassiveCandidateSubcircuit();
  const b11PassiveCandidateExperiment = buildB11PassiveCandidateExperiment(options);
  const components = [
    ...commandPortComponents(resolved),
    ...b6Inventory.components,
    ...b6NumericalComponents(params),
    ...s6CoreComponents(),
  ];
  const b6Passive = b6PassiveChecks();

  return {
    title: "Siemens U273 orchestrated parametric netlist",
    version: "u273_orchestrator_v0_1",
    generated_at: new Date().toISOString(),
    status: STATUS.PARTIAL,
    coupling_mode: resolved.couplingMode,
    scientific_boundary:
      "The current executable DC netlist keeps the B11/S6/S7 command path as a finite Thevenin reference. Full active B11+B6 closure waits for unread transistor routes and complete B11 netlist proof.",
    selected_modes: {
      debug_config_id: resolved.debugConfigId,
      debug_config_mode: resolved.debugConfigMode,
      s6_position: resolved.s6Position,
      s7_mode: resolved.s7Mode,
      command_source_volt: resolved.commandSourceVolt,
      command_source_ohm: resolved.b6.commandSourceOhm,
    },
    parameters: {
      b6: Object.fromEntries(Object.entries(params).map(([key, value]) => [key, finiteOrString(value)])),
    },
    topology_step1: {
      debug_config: buildDebugConfig(resolved),
      switch_contact_candidates: switchContactCandidates,
      b11_s6_s7_switch_matrix_candidate: b11S6S7SwitchMatrixCandidate,
      b11_visual_inventory: b11VisualInventory,
      b11_local_topology_proof_ledger: b11LocalTopologyProofLedger,
      b11_pdf_topology_evidence_ledger: b11PdfTopologyEvidenceLedger,
      b11_pdf_text_functional_evidence: b11PdfTextFunctionalEvidence,
      b11_gpt_reponse_2_promotion_audit: b11GptReponse2PromotionAudit,
      b11_scientific_activation_research_ledger: b11ScientificActivationResearchLedger,
      b11_pdf_active_hypothesis_constraints: b11PdfActiveHypothesisConstraints,
      b11_retranscription_crosscheck: b11RetranscriptionCrossCheck,
      b11_prior_artifact_audit: b11PriorArtifactAudit,
      b11_direct_printed_node_pin_prefilter: b11DirectPrintedNodePinPrefilter,
      b11_d1_d2_polarity_experiment: b11D1D2PolarityExperiment,
      b11_passive_candidate_subcircuit: b11PassiveCandidateSubcircuit,
      b11_passive_candidate_experiment: b11PassiveCandidateExperiment,
      local_dc_checkpoints: [
        b6OutputBiasDcCheckpoint(),
      ],
      b11_status: {
        status: STATUS.PARTIAL,
        inventory_required: true,
        active_simulation_required_for_step1: false,
        note:
          "B11 remains too reduced for closure. Existing R15/R16/R13 notes are useful candidates, but not a full visual netlist proof.",
      },
    },
    components,
    nodes: collectNodes(components),
    schematic_inventories: {
      B6: b6Inventory,
    },
    switches: {
      S6: {
        status: STATUS.PARTIAL,
        selected_position: resolved.s6Position,
        truth_table_status: STATUS.UNKNOWN,
        delivery_linear_reading: {
          status: STATUS.SWITCH_CONTACT_CANDIDATE,
          contacts: ["4-5 closed", "8-9 closed", "5-6 open", "7-8 open", "5-3-1 closed", "2 open"],
          contact_candidates: switchContactCandidates.contacts.filter((entry) => entry.switch === "S6"),
          source_etape: "Etape 20/21/24",
          boundary: "Use only as the drawn/debug position; do not promote to a complete S6 truth table.",
        },
        unresolved: [
          "All non-delivery Vorentzerrung positions still require contact-table proof.",
          "R1/R2 routing by every S6 position is still bounded, not final.",
        ],
        contact_hypotheses: s6ContactHypotheses,
      },
      S7: {
        status: STATUS.PARTIAL,
        selected_mode: resolved.s7Mode,
        truth_table_status: STATUS.UNKNOWN,
        selected_contact_candidates: switchContactCandidates.contacts.filter((entry) => entry.switch === "S7"),
        contact_table: s7ContactTable,
        detector_scenarios: detectorScenarios.map((scenario) => ({
          name: scenario.name,
          mode: scenario.mode,
          s7_position: scenario.s7Position,
          s7_closed: scenario.s7Closed,
          zener_path: scenario.zenerPath,
          status: scenario.mode === "limiter" ? STATUS.SWITCH_CONTACT_CANDIDATE : STATUS.HYPOTHESIS,
          note: scenario.note,
        })),
      },
    },
    diode_models: {
      B6_bridge: {
        status: STATUS.PARTIAL,
        law: u273EmpiricalDiodeLaw,
        source_etape: "Etape 17/23",
        warning: "Law is closed; final bridge polarity remains photo-dependent.",
      },
      B11_detector: {
        status: STATUS.PARAMETRIC,
        models: ["ZL10 threshold sweep", "SSD55 fixed drop sweep", "B30 C2200 full-wave threshold approximation"],
        source_etape: "Etape 22/24",
      },
    },
    transistor_models: {
      B6: {
        status: STATUS.PARTIAL,
        rule:
          "Expose BJT candidates and bounds; do not stamp them unless an explicit conditional hypothesis selects proven terminals.",
        bjt_candidates: b6BjtCandidates(),
        passive_checks: b6Passive,
        hybrid_pi_candidates: b6TransistorHybridPiRows(),
        output_stage_bounds: b6OutputStageBounds(),
        symbolic_boundaries: b6SymbolicTransistorBoundaries(b6Passive),
        source_etape: "Etape 24",
      },
      B11: {
        status: STATUS.UNCONFIRMED,
        note: "Ts1/Ts2 hypotheses remain delegated to b11_ts1_ts2_pin_hypothesis_solver.js until photo proof.",
        source_etape: "Etape 8/24",
      },
    },
    dc_execution: {
      status: "THEVENIN_REFERENCE_EXECUTABLE",
      netlist: dcNetlist,
    },
  };
}

function solveOperatingPoint(options = {}, solverOptions = {}) {
  const dcNetlist = buildDcNetlist(options);
  const result = solveDc(dcNetlist, {
    damping: solverOptions.damping ?? 0.35,
    tolerance: solverOptions.tolerance ?? 1e-10,
    maxIterations: solverOptions.maxIterations ?? 260,
    initialGuess: solverOptions.initialGuess,
  });
  return {
    netlist: dcNetlist,
    result,
    solution: solutionObject(result, dcNetlist),
  };
}

function writeSnapshot(options = {}) {
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "u273_orchestrator_snapshot.json");
  fs.writeFileSync(filePath, JSON.stringify(buildNetlist(options), null, 2));
  return filePath;
}

function run() {
  const filePath = writeSnapshot();
  console.log(`Wrote ${filePath}`);
}

if (require.main === module) {
  run();
}

module.exports = {
  STATUS,
  DEFAULTS,
  resolveOptions,
  b6OutputBiasDcCheckpoint,
  buildDebugConfig,
  buildSwitchContactCandidates,
  buildB11S6S7SwitchMatrixCandidate,
  buildB11VisualInventory,
  buildB11LocalTopologyProofLedger,
  buildB11PdfTopologyEvidenceLedger,
  buildB11PdfTextFunctionalEvidence,
  buildB11GptReponse2PromotionAudit,
  buildB11ScientificActivationResearchLedger,
  buildB11PdfActiveHypothesisConstraints,
  buildB11RetranscriptionCrossCheck,
  buildB11PriorArtifactAudit,
  buildB11DirectPrintedNodePinPrefilter,
  buildB11D1D2PolarityExperiment,
  buildB11PassiveCandidateSubcircuit,
  buildB11PassiveCandidateExperiment,
  buildDcNetlist,
  buildNetlist,
  solveOperatingPoint,
  writeSnapshot,
  run,
};
