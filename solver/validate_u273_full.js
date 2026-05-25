"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { runGlobalDc } = require("./u273_dc_global");
const { runGlobalAc } = require("./u273_ac_global");
const { runTransient } = require("./u273_transient");
const {
  buildNetlist,
  buildB11D1D2PolarityExperiment,
  buildB11PassiveCandidateExperiment,
} = require("./u273_orchestrator");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function run() {
  const dc = runGlobalDc();
  const ac = runGlobalAc();
  const transient = runTransient();
  const guardedNetlist = buildNetlist();
  const defaultB11D1D2Experiment = guardedNetlist.topology_step1?.b11_d1_d2_polarity_experiment;
  const enabledB11D1D2Experiment = buildB11D1D2PolarityExperiment({
    enableB11D1D2PolarityExperiment: true,
  });
  const defaultB11PassiveExperiment = guardedNetlist.topology_step1?.b11_passive_candidate_experiment;
  const enabledB11PassiveExperiment = buildB11PassiveCandidateExperiment({
    enableB11PassiveCandidateExperiment: true,
  });
  const failures = [];

  if (!dc.payload.results.every((row) => row.converged)) {
    failures.push("DC: at least one scenario did not converge");
  }
  if (!dc.payload.results.every((row) => row.residual && row.residual.maxAbs <= 1e-9)) {
    failures.push("DC: at least one scenario has excessive residual");
  }
  if (!ac.payload.rows.length) failures.push("AC: no rows generated");
  if (!ac.payload.rows.some((row) => row.frequency === 1000 && row.driveVolt === 3)) {
    failures.push("AC: missing 1 kHz / 3 V reference rows");
  }
  if (!transient.payload.rows.length) failures.push("Transient: no rows generated");
  if (!transient.payload.summaries.every((row) => row.maxCmdVolt >= 0)) {
    failures.push("Transient: invalid command voltage summary");
  }
  if (guardedNetlist.components.length !== 88) {
    failures.push("Topology step 1: official executable component count changed from 88");
  }
  if (!defaultB11PassiveExperiment || defaultB11PassiveExperiment.enabled !== false) {
    failures.push("Topology step 1: B11 passive experiment must be disabled by default");
  }
  if (defaultB11PassiveExperiment?.result !== null) {
    failures.push("Topology step 1: default B11 passive experiment must not carry opt-in results");
  }
  if (!defaultB11D1D2Experiment || defaultB11D1D2Experiment.enabled !== false) {
    failures.push("Topology step 1: B11 D1/D2 polarity experiment must be disabled by default");
  }
  if (defaultB11D1D2Experiment?.result !== null) {
    failures.push("Topology step 1: default B11 D1/D2 polarity experiment must not carry opt-in results");
  }
  const b11LocalLedger = guardedNetlist.topology_step1?.b11_local_topology_proof_ledger;
  const b11LocalKcl = b11LocalLedger?.local_kcl_checkpoint;
  if (!b11LocalLedger || b11LocalLedger.status !== "CANDIDAT_SCHEMA_PARTIEL") {
    failures.push("Topology step 1: B11 local proof ledger must remain partial schematic candidate");
  }
  if (b11LocalLedger?.canonical_value_reading_ohm?.R15 !== 1200 ||
      b11LocalLedger?.canonical_value_reading_ohm?.R16 !== 1200) {
    failures.push("Topology step 1: B11 local proof ledger must lock R15/R16 at 1.2 kOhm");
  }
  if (!b11LocalLedger?.rejected_value_readings?.some((row) =>
    row.component === "R16" && row.rejected_value_ohm === 12000 && row.accepted_value_ohm === 1200)) {
    failures.push("Topology step 1: B11 local proof ledger must reject the 12 kOhm R16 reading");
  }
  if (b11LocalKcl?.closure_status !== "not_closed_by_R15_R16_R13_alone") {
    failures.push("Topology step 1: B11 R15/R16/R13 KCL must remain explicitly non-closed");
  }
  if (!b11LocalKcl || Math.abs(b11LocalKcl.residual_iR15_minus_iR16_minus_iR13_amp - 0.000378030303030303) > 1.0e-12) {
    failures.push("Topology step 1: B11 R15/R16/R13 residual must remain near 0.378 mA");
  }
  if (!b11LocalLedger?.route_proof_table?.some((row) =>
    row.node === "B11_S6_S7_C11_CMD" && row.confidence === "MODE_DEPENDENT_CANDIDATE_SWITCH_NETWORK")) {
    failures.push("Topology step 1: B11 S6/S7/C11/CMD must remain mode-dependent");
  }
  const switchMatrix = guardedNetlist.topology_step1?.b11_s6_s7_switch_matrix_candidate;
  if (!switchMatrix || switchMatrix.switch_matrix_status !== "SWITCH_MATRIX_CANDIDATE") {
    failures.push("Topology step 1: B11 S6/S7 must be recorded as a switch matrix candidate");
  }
  if (switchMatrix?.truth_table_status !== "UNKNOWN" ||
      switchMatrix?.currently_encoded_contacts_are_candidates_only !== true) {
    failures.push("Topology step 1: B11 S6/S7 truth table must remain unknown and candidate-only");
  }
  if (switchMatrix?.known_network_values?.C1_farad !== 3e-9 ||
      switchMatrix?.known_network_values?.C2_farad !== 3e-9 ||
      switchMatrix?.known_network_values?.C3_farad !== 3.3e-9 ||
      switchMatrix?.known_network_values?.C4_farad !== 3e-9) {
    failures.push("Topology step 1: B11 S6 preemphasis capacitor values must stay at the PDF-read candidates");
  }
  if (switchMatrix?.known_network_values?.R3_ohm !== 7500 ||
      switchMatrix?.known_network_values?.R4_ohm !== 7500 ||
      switchMatrix?.known_network_values?.R5_ohm !== 15000 ||
      switchMatrix?.known_network_values?.R6_ohm !== 3900) {
    failures.push("Topology step 1: B11 S6 preemphasis resistor values must stay at the PDF-read candidates");
  }
  if (!switchMatrix?.forbidden_shortcuts?.includes("S6 = mode_limiter") ||
      !switchMatrix?.forbidden_shortcuts?.includes("S7 = mode_compressor")) {
    failures.push("Topology step 1: B11 S6/S7 matrix must forbid premature mode labels");
  }
  if (!switchMatrix?.per_position_required_fields?.includes("effect_on_CMD_D1_D2")) {
    failures.push("Topology step 1: B11 S6/S7 matrix must require CMD/D1/D2 effect proof per position");
  }
  const b11PdfEvidence = guardedNetlist.topology_step1?.b11_pdf_topology_evidence_ledger;
  if (!b11PdfEvidence || b11PdfEvidence.id !== "B11_PDF_TOPOLOGY_EVIDENCE_STEP1") {
    failures.push("Topology step 1: B11 PDF topology evidence ledger is missing");
  }
  if (b11PdfEvidence?.mna_action !== "pdf_evidence_only_not_stamped") {
    failures.push("Topology step 1: B11 PDF evidence ledger must remain non-stamped");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "R13" && row.visual_route === "B11_N9 -> R13 220k -> B11_N05_Ts1_LOW")) {
    failures.push("Topology step 1: B11 PDF evidence must confirm R13 N9-to-N05 route");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "R16" && row.visual_route === "B11_N9 -> R16 1.2k -> B11_REF")) {
    failures.push("Topology step 1: B11 PDF evidence must confirm R16 N9-to-reference route");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "R31" && row.visual_route === "B11_ND2_BOT_RAW -> R31 51k -> B11_NCMD_LOCAL")) {
    failures.push("Topology step 1: B11 PDF evidence must confirm R31 detector-to-local-NCMD route");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "D2_SSD55" &&
    row.anode_candidate === "B11_N20_D2_TOP" &&
    row.cathode_candidate === "B11_ND2_BOT_RAW" &&
    row.spice_polarity_promoted === false)) {
    failures.push("Topology step 1: B11 PDF evidence must keep D2 SSD55 polarity guarded");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "D1_ZL10" &&
    row.visual_route === "B11_NCMD_LOCAL -> D1 ZL10 -> B11_NZENER_OUT_18_19" &&
    row.anode_candidate === "B11_NZENER_OUT_18_19" &&
    row.cathode_candidate === "B11_NCMD_LOCAL" &&
    row.bv_volt_candidate === 10 &&
    row.functional_role_from_pdf_text === "limiter_threshold_zener" &&
    row.spice_polarity_promoted === false)) {
    failures.push("Topology step 1: B11 PDF evidence must confirm D1 ZL10 local command route");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "C7" &&
    row.visual_route === "B11_NDRV_C78_CANDIDATE -> C7 25u -> B11_N215" &&
    row.endpoint_coupling_limit?.includes("NDRV_C9"))) {
    failures.push("Topology step 1: B11 PDF evidence must keep C7 driver-side coupling guarded");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "C8" &&
    row.visual_route === "B11_NDRV_C78_CANDIDATE -> C8 25u -> B11_N145")) {
    failures.push("Topology step 1: B11 PDF evidence must keep C8 on the guarded NDRV_C78 candidate");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "C9" &&
    row.visual_route === "B11_NDRV_C9_CANDIDATE -> C9 25u -> B11_N9")) {
    failures.push("Topology step 1: B11 PDF evidence must keep C9 separate from NDRV_C78");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "Ts2" && row.pinout_status === "BCE_UNASSIGNED_GUARDED")) {
    failures.push("Topology step 1: B11 PDF evidence must keep Ts2 B/C/E unassigned");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "C5" && row.dc_policy === "open_in_dc_between_N05_and_R7_R8_S6")) {
    failures.push("Topology step 1: B11 PDF evidence must keep C5 as the DC-open N05-to-R7/R8/S6 route");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "C11" && row.role === "rectifier_filter_storage_not_direct_cmd")) {
    failures.push("Topology step 1: B11 PDF evidence must classify C11 as rectifier storage, not direct CMD proof");
  }
  if (b11PdfEvidence?.closure_verdict?.active_b11_promotable !== false ||
      b11PdfEvidence?.closure_verdict?.replace_thevenin_b11_s6_s7_cmd !== false) {
    failures.push("Topology step 1: B11 PDF evidence must not promote active B11 or replace the Thevenin command port");
  }
  if (!b11PdfEvidence?.proof_rows?.some((row) =>
    row.component === "CMD_ROUTE" &&
    row.boundary_trace_confirmed === true &&
    row.replace_thevenin_b11_s6_s7_cmd === false)) {
    failures.push("Topology step 1: B11 PDF evidence must keep CMD route as boundary-confirmed but Thevenin-guarded");
  }
  const b11PdfTextFunctional = guardedNetlist.topology_step1?.b11_pdf_text_functional_evidence;
  if (!b11PdfTextFunctional || b11PdfTextFunctional.id !== "B11_PDF_TEXT_FUNCTIONAL_EVIDENCE_STEP1") {
    failures.push("Topology step 1: B11 PDF text functional evidence is missing");
  }
  if (b11PdfTextFunctional?.mna_action !== "functional_evidence_only_not_stamped" ||
      b11PdfTextFunctional?.active_simulation_allowed !== false) {
    failures.push("Topology step 1: B11 PDF text functional evidence must remain non-stamped");
  }
  if (!b11PdfTextFunctional?.confirmed_functional_facts?.some((row) =>
    row.id === "B11_REGELVERSTAERKER_FEEDBACK_FROM_B6_OUTPUT")) {
    failures.push("Topology step 1: PDF text must confirm B11 feedback role");
  }
  if (!b11PdfTextFunctional?.confirmed_functional_facts?.some((row) =>
    row.id === "LIMITER_ZL10_THRESHOLD")) {
    failures.push("Topology step 1: PDF text must confirm D1 ZL10 limiter threshold role");
  }
  if (!b11PdfTextFunctional?.confirmed_functional_facts?.some((row) =>
    row.id === "S7_BYPASSES_ZL10_IN_COMPRESSOR")) {
    failures.push("Topology step 1: PDF text must confirm the S7 compressor bypass function");
  }
  if (!b11PdfTextFunctional?.confirmed_functional_facts?.some((row) =>
    row.id === "S6_PREEMPHASIS_SWITCHING")) {
    failures.push("Topology step 1: PDF text must confirm S6 preemphasis switching function");
  }
  if (b11PdfTextFunctional?.mode_logic_candidates?.limiter?.contact_truth_table_promoted !== false ||
      b11PdfTextFunctional?.mode_logic_candidates?.compressor?.contact_truth_table_promoted !== false) {
    failures.push("Topology step 1: B11 PDF text must not promote S7 mode contact truth tables");
  }
  if (b11PdfTextFunctional?.empirical_diode_branch_law?.scope !==
      "B6 audio gain-control diode bridge branch, not D1_ZL10 or D2_SSD55 detector diodes") {
    failures.push("Topology step 1: Siemens empirical diode law must stay scoped to the audio gain bridge");
  }
  if (b11PdfTextFunctional?.empirical_diode_branch_law?.bridge_signal_target_mV?.nominal !== 25) {
    failures.push("Topology step 1: PDF text diode bridge target must keep the 25 mV nominal signal");
  }
  if (b11PdfTextFunctional?.dynamic_plausibility_targets?.attack_ms?.limiter !== 0.5 ||
      b11PdfTextFunctional?.dynamic_plausibility_targets?.attack_ms?.compressor !== 1.0 ||
      b11PdfTextFunctional?.dynamic_plausibility_targets?.release_s?.max !== 1.5) {
    failures.push("Topology step 1: PDF text attack/release targets must be preserved");
  }
  if (!b11PdfTextFunctional?.not_confirmed_from_text?.includes("Complete S6/S7 contact truth table.")) {
    failures.push("Topology step 1: PDF text evidence must keep S6/S7 truth table unconfirmed");
  }
  if (b11PdfTextFunctional?.closure_verdict?.active_b11_promotable !== false ||
      b11PdfTextFunctional?.closure_verdict?.replace_thevenin_b11_s6_s7_cmd !== false) {
    failures.push("Topology step 1: PDF text evidence must not promote active B11 or replace the Thevenin command port");
  }
  const b11GptReponse2Audit = guardedNetlist.topology_step1?.b11_gpt_reponse_2_promotion_audit;
  if (!b11GptReponse2Audit || b11GptReponse2Audit.id !== "B11_GPT_REPONSE_2_2105_PROMOTION_AUDIT") {
    failures.push("Topology step 1: GPT reponse 2 promotion audit is missing");
  }
  if (b11GptReponse2Audit?.mna_action !== "audit_only_not_stamped" ||
      b11GptReponse2Audit?.active_simulation_allowed !== false) {
    failures.push("Topology step 1: GPT reponse 2 audit must remain non-stamped");
  }
  if (!b11GptReponse2Audit?.claims_accepted_as_guarded_constraints?.some((row) =>
    row.claim.includes("D1_ZL10 anode=NZENER_OUT"))) {
    failures.push("Topology step 1: GPT reponse 2 audit must record the guarded D1 polarity agreement");
  }
  if (!b11GptReponse2Audit?.claims_rejected_or_not_promotable?.some((row) =>
    row.claim === "C7.right = +24V rail" &&
    row.project_verdict === "REJECT_FOR_STEP1" &&
    row.reason.includes("B11_N215"))) {
    failures.push("Topology step 1: GPT reponse 2 audit must reject C7-to-24V promotion");
  }
  if (!b11GptReponse2Audit?.claims_rejected_or_not_promotable?.some((row) =>
    row.claim === "B11 can move to partial_promoted_guarded official components" &&
    row.project_verdict === "REJECT_FOR_STEP1")) {
    failures.push("Topology step 1: GPT reponse 2 audit must reject premature B11 activation");
  }
  if (b11GptReponse2Audit?.canonical_project_decisions_after_audit?.c7_endpoint !==
      "B11_NDRV_C78_CANDIDATE -> B11_N215") {
    failures.push("Topology step 1: GPT reponse 2 audit must keep the canonical C7 endpoint at B11_N215");
  }
  if (b11GptReponse2Audit?.canonical_project_decisions_after_audit?.b11_activation_status !==
      "blocked_before_active_promotion") {
    failures.push("Topology step 1: GPT reponse 2 audit must keep B11 blocked before active promotion");
  }
  if (b11GptReponse2Audit?.closure_verdict?.c7_to_v24_rejected !== true ||
      b11GptReponse2Audit?.closure_verdict?.active_b11_promotable !== false ||
      b11GptReponse2Audit?.closure_verdict?.add_official_b11_components_now !== false) {
    failures.push("Topology step 1: GPT reponse 2 audit must block C7-to-24V and official B11 component promotion");
  }
  const b11ScientificResearch = guardedNetlist.topology_step1?.b11_scientific_activation_research_ledger;
  if (!b11ScientificResearch ||
      b11ScientificResearch.id !== "B11_SCIENTIFIC_ACTIVATION_RESEARCH_LEDGER") {
    failures.push("Topology step 1: B11 scientific activation research ledger must be present");
  }
  if (b11ScientificResearch?.mna_action !== "research_requirements_only_not_stamped" ||
      b11ScientificResearch?.active_simulation_allowed !== false ||
      b11ScientificResearch?.executable_netlist_effect !== "none") {
    failures.push("Topology step 1: B11 scientific research must stay non-stamped and non-executable");
  }
  if (!b11ScientificResearch?.scientific_sources?.some((row) =>
    row.id === "NGSPICE_DIODE_BJT_MODEL_REFERENCE" &&
    row.project_consequence.includes("zener breakdown branch"))) {
    failures.push("Topology step 1: research ledger must require a zener-capable diode model before D1 stamping");
  }
  if (!b11ScientificResearch?.required_stamps_before_b11_activation?.some((row) =>
    row.device === "zener_diode" &&
    row.minimum_parameters?.includes("BV") &&
    row.minimum_parameters?.includes("IBV"))) {
    failures.push("Topology step 1: research ledger must list BV/IBV for the zener stamp");
  }
  if (!b11ScientificResearch?.component_data_ledger?.some((row) =>
    row.component === "D1_ZL10" &&
    row.model_candidate?.BV_volt === 10 &&
    row.promotion_allowed_now === false)) {
    failures.push("Topology step 1: research ledger must keep D1 ZL10 as a non-promoted 10 V zener candidate");
  }
  if (!b11ScientificResearch?.component_data_ledger?.some((row) =>
    row.component === "D2_SSD55" &&
    row.source_strength === "weak_identity_only" &&
    row.promotion_allowed_now === false)) {
    failures.push("Topology step 1: research ledger must record SSD55 data as too weak for promotion");
  }
  if (!b11ScientificResearch?.component_data_ledger?.some((row) =>
    row.component === "SST117" &&
    row.source_strength === "identity_and_polarity_only" &&
    row.promotion_allowed_now === false)) {
    failures.push("Topology step 1: research ledger must keep SST117 model data guarded");
  }
  if (b11ScientificResearch?.closure_verdict?.zener_model_required_before_d1_stamp !== true ||
      b11ScientificResearch?.closure_verdict?.gummel_poon_required_now !== false ||
      b11ScientificResearch?.closure_verdict?.active_b11_promotable !== false ||
      b11ScientificResearch?.closure_verdict?.replace_thevenin_b11_s6_s7_cmd !== false) {
    failures.push("Topology step 1: research ledger must require zener work while blocking active B11 promotion");
  }
  const b11PdfActiveConstraints = guardedNetlist.topology_step1?.b11_pdf_active_hypothesis_constraints;
  if (!b11PdfActiveConstraints || b11PdfActiveConstraints.mna_action !== "hypothesis_constraints_only_not_stamped") {
    failures.push("Topology step 1: B11 PDF active constraints must remain non-stamped hypothesis constraints");
  }
  if (!b11PdfActiveConstraints?.current_constraints?.some((row) =>
    row.node === "B11_N145" &&
    row.required_action === "sink_current_from_printed_node" &&
    Math.abs(row.magnitude_abs_amp - 0.007916666666666667) < 1.0e-15)) {
    failures.push("Topology step 1: B11_N145 active constraint must require about 7.92 mA sink current");
  }
  if (!b11PdfActiveConstraints?.current_constraints?.some((row) =>
    row.node === "B11_N9" &&
    row.required_action === "source_current_into_printed_node" &&
    Math.abs(row.magnitude_abs_amp - 0.007538636363636364) < 1.0e-15)) {
    failures.push("Topology step 1: B11_N9 active constraint must require about 7.54 mA source current");
  }
  if (!b11PdfActiveConstraints?.current_constraints?.some((row) =>
    row.node === "B11_N05" &&
    row.required_action === "source_current_into_printed_node" &&
    Math.abs(row.magnitude_abs_amp - 0.004136958874458874) < 1.0e-15)) {
    failures.push("Topology step 1: B11_N05 active constraint must require about 4.14 mA source current");
  }
  if (b11PdfActiveConstraints?.forbidden_use?.includes("Do not infer B/C/E") !== true) {
    failures.push("Topology step 1: B11 PDF active constraints must forbid B/C/E inference");
  }
  const b11Retranscription = guardedNetlist.topology_step1?.b11_retranscription_crosscheck;
  if (!b11Retranscription || b11Retranscription.mna_action !== "crosscheck_only_not_stamped") {
    failures.push("Topology step 1: B11 UTF-8 retranscription must remain a non-stamped cross-check");
  }
  if (b11Retranscription?.source_encoding !== "UTF-8") {
    failures.push("Topology step 1: B11 retranscription cross-check must record UTF-8 source handling");
  }
  if (!b11Retranscription?.agrees_with_pdf_ledger?.some((row) =>
    row.item === "R13" && row.project_status.includes("B11_N9 -> R13 -> B11_N05_Ts1_LOW"))) {
    failures.push("Topology step 1: B11 retranscription cross-check must agree with the R13 PDF route");
  }
  if (!b11Retranscription?.useful_new_checklist_items?.some((row) =>
    row.item === "B11.R31/D1/D2/NCMD" && row.action.includes("disabled candidate components"))) {
    failures.push("Topology step 1: B11 retranscription cross-check must record R31/D1/D2/NCMD as captured but guarded");
  }
  if (!b11Retranscription?.conflicts_or_unverified_items?.some((row) =>
    row.item === "C11 to CMD" && row.verdict.includes("do not replace"))) {
    failures.push("Topology step 1: B11 retranscription cross-check must not promote C11/CMD");
  }
  if (!b11Retranscription?.next_pdf_proof_targets?.includes("Whether NDRV_C78 and NDRV_C9 are separate nodes or a common conductor")) {
    failures.push("Topology step 1: B11 retranscription cross-check must keep NDRV_C78 and NDRV_C9 unresolved");
  }
  const pdfEvidenceDir = path.join(__dirname, "..", "results", "pdf_evidence");
  for (const crop of [
    "crop_b11_ts2_r15_r16_c8_c9_close.png",
    "crop_b11_c5_c11_cmd_wide.png",
    "crop_b11_c5_s6_close.png",
    "crop_b11_c11_detector_cmd_area.png",
    "crop_b11_s6_to_b6_boundary_cmd_trace.png",
  ]) {
    if (!fs.existsSync(path.join(pdfEvidenceDir, crop))) {
      failures.push(`Topology step 1: missing B11 PDF evidence crop ${crop}`);
    }
  }
  const b11PriorAudit = guardedNetlist.topology_step1?.b11_prior_artifact_audit;
  if (!b11PriorAudit || b11PriorAudit.artifact_count < 4) {
    failures.push("Topology step 1: B11 prior artifact audit must list superseded/conflicting scripts");
  }
  if (!b11PriorAudit?.artifacts?.some((row) =>
    row.artifact === "solver/b11_ts2_r15_to_n05_path_solver.js" &&
    row.status === "CONFLICTS_WITH_STEP1_CANONICAL_TOPOLOGY")) {
    failures.push("Topology step 1: B11 R16/R17 conflicting prior script must be quarantined");
  }
  if (!b11PriorAudit?.artifacts?.some((row) =>
    row.artifact === "solver/b11_ts1_ts2_topology_constraints.js" &&
    row.issue_class === "dc_path_superseded_by_C5_open_reading")) {
    failures.push("Topology step 1: B11 C5/R7/R8 superseded prior script must be quarantined");
  }
  const b11PinPrefilter = guardedNetlist.topology_step1?.b11_direct_printed_node_pin_prefilter;
  if (!b11PinPrefilter || b11PinPrefilter.tested_hypothesis_count !== 96) {
    failures.push("Topology step 1: B11 direct printed-node pin prefilter must test 96 Ts1/Ts2 hypotheses");
  }
  if (b11PinPrefilter?.soft_active_candidate_count !== 0 || b11PinPrefilter?.strict_active_candidate_count !== 0) {
    failures.push("Topology step 1: direct printed-node B11 pin prefilter must not produce accepted candidates");
  }
  if (b11PinPrefilter?.mna_action !== "prefilter_only_not_stamped") {
    failures.push("Topology step 1: B11 direct printed-node pin prefilter must remain non-stamped");
  }
  if (b11PinPrefilter?.forbidden_use?.includes("do not accept any BJT pinout") !== true) {
    failures.push("Topology step 1: B11 direct printed-node pin prefilter must block pinout acceptance");
  }
  if (!enabledB11D1D2Experiment.enabled) {
    failures.push("Topology step 1: explicit B11 D1/D2 polarity experiment did not enable");
  }
  if (enabledB11D1D2Experiment.modifies_official_components !== false ||
      enabledB11D1D2Experiment.isolated_from_executable_netlist !== true) {
    failures.push("Topology step 1: B11 D1/D2 polarity experiment must remain isolated and non-executable");
  }
  const d1d2Result = enabledB11D1D2Experiment.result;
  if (d1d2Result?.candidate_count !== 4 ||
      d1d2Result?.functional_pass_candidate_count !== 1 ||
      d1d2Result?.rejected_candidate_count !== 3) {
    failures.push("Topology step 1: B11 D1/D2 polarity experiment must test 4 pairs with one guarded functional pass");
  }
  if (d1d2Result?.accepted_for_future_guarded_mna_stamp !==
      "D1_ZL10_GRAPHIC_CANDIDATE__D2_SSD55_GRAPHIC_CANDIDATE") {
    failures.push("Topology step 1: B11 D1/D2 polarity experiment must keep only the graphic pair as the future guarded stamp candidate");
  }
  const d1d2Pass = d1d2Result?.candidate_rows?.find((row) =>
    row.id === "D1_ZL10_GRAPHIC_CANDIDATE__D2_SSD55_GRAPHIC_CANDIDATE");
  if (!d1d2Pass ||
      d1d2Pass.status !== "FUNCTIONAL_PASS_GUARDED_NOT_PROMOTED" ||
      d1d2Pass.spice_polarity_promoted !== false ||
      d1d2Pass.b11_ncmd_local_separate_from_b6_cmd !== true ||
      Math.abs(d1d2Pass.thresholdSeparationPeakVolt - 10.0) > 1.0e-12) {
    failures.push("Topology step 1: B11 D1/D2 graphic polarity pair must pass functionally without promotion");
  }
  if (!d1d2Result?.candidate_rows?.some((row) =>
    row.id === "D1_ZL10_REVERSED_CONTROL__D2_SSD55_GRAPHIC_CANDIDATE" &&
    row.status === "REJECTED_BY_FUNCTIONAL_POLARITY_RULES")) {
    failures.push("Topology step 1: B11 D1 reversed control must be rejected by S7/zener functional rules");
  }
  if (!d1d2Result?.candidate_rows?.some((row) =>
    row.id === "D1_ZL10_GRAPHIC_CANDIDATE__D2_SSD55_REVERSED_CONTROL" &&
    row.status === "REJECTED_BY_FUNCTIONAL_POLARITY_RULES")) {
    failures.push("Topology step 1: B11 D2 reversed control must be rejected by positive detector conduction");
  }
  if (d1d2Result?.closure_verdict?.spice_polarity_promoted !== false ||
      d1d2Result?.closure_verdict?.s7_contact_truth_table_promoted !== false ||
      d1d2Result?.closure_verdict?.replace_thevenin_b11_s6_s7_cmd !== false ||
      d1d2Result?.closure_verdict?.b11_ncmd_local_separate_from_b6_cmd !== true) {
    failures.push("Topology step 1: B11 D1/D2 polarity experiment must block promotion and keep NCMD local");
  }
  if (!enabledB11PassiveExperiment.enabled) {
    failures.push("Topology step 1: explicit B11 passive experiment did not enable");
  }
  if (!enabledB11PassiveExperiment.result?.isolated_dc_solve?.converged) {
    failures.push("Topology step 1: B11 passive experiment isolated DC probe did not converge");
  }
  if (enabledB11PassiveExperiment.result?.stamped_resistor_count !== 7) {
    failures.push("Topology step 1: B11 passive experiment must stamp exactly 7 DC resistive branches");
  }
  if (enabledB11PassiveExperiment.result?.excluded_component_count !== 13) {
    failures.push("Topology step 1: B11 passive experiment must exclude exactly 13 guarded/open components");
  }
  const b11Requirements = enabledB11PassiveExperiment.result?.active_current_requirements ?? [];
  if (b11Requirements.length !== 5) {
    failures.push("Topology step 1: B11 passive experiment must produce 5 non-rail current constraints");
  }
  const b11RequirementSummary = enabledB11PassiveExperiment.result?.active_current_requirement_summary;
  if (b11RequirementSummary?.largest_constraint_node !== "B11_N145") {
    failures.push("Topology step 1: B11 passive current constraints must keep N145 as the largest missing current");
  }
  if (b11RequirementSummary?.boundary?.includes("not transistor pin assignments") !== true) {
    failures.push("Topology step 1: B11 passive current constraints must block pin-assignment promotion");
  }
  const n9Requirement = b11Requirements.find((row) => row.node === "B11_N9");
  if (!n9Requirement || n9Requirement.required_action !== "source_current_into_printed_node") {
    failures.push("Topology step 1: B11_N9 passive constraint must require source current into the node");
  }
  const n145Requirement = b11Requirements.find((row) => row.node === "B11_N145");
  if (!n145Requirement || n145Requirement.required_action !== "sink_current_from_printed_node") {
    failures.push("Topology step 1: B11_N145 passive constraint must require sink current from the node");
  }
  const b11Cross = enabledB11PassiveExperiment.result?.cross_checks;
  if (!b11Cross || Math.abs(b11Cross.n9PassiveDemandAmp - b11Cross.n9PassiveDemandReferenceAmp) > 1.0e-15) {
    failures.push("Topology step 1: B11 passive experiment N9 KCL cross-check drifted");
  }
  if (!b11Cross || Math.abs(b11Cross.n05DeficitAfterR13Amp - b11Cross.n05DeficitAfterR13ReferenceAmp) > 1.0e-15) {
    failures.push("Topology step 1: B11 passive experiment N05 KCL cross-check drifted");
  }

  const report = {
    title: "Siemens U273 full validation report",
    status: failures.length ? "FAIL" : "PASS_WITH_GUARDED_BOUNDARIES",
    boundary:
      "PASS means the rigorous current chain runs: DC Thevenin reference, AC bridge/command small-signal, and bounded quasi-static transient. It does not mean full active BJT closure.",
    generated: {
      dc: [dc.jsonPath, dc.csvPath],
      ac: [ac.jsonPath, ac.csvPath],
      transient: [transient.jsonPath, transient.csvPath],
    },
    summary: {
      dcScenarios: dc.payload.results.length,
      acRows: ac.payload.rows.length,
      transientRows: transient.payload.rows.length,
      transientCases: transient.payload.summaries.length,
      officialExecutableComponents: guardedNetlist.components.length,
      b11LocalLedgerStatus: b11LocalLedger?.status ?? null,
      b11LocalR15R16ResidualAmp:
        b11LocalKcl?.residual_iR15_minus_iR16_minus_iR13_amp ?? null,
      b11PdfEvidenceStatus: b11PdfEvidence?.status ?? null,
      b11PdfEvidenceActivePromotable:
        b11PdfEvidence?.closure_verdict?.active_b11_promotable ?? null,
      b11PdfActiveConstraintRows:
        b11PdfActiveConstraints?.current_constraints?.length ?? 0,
      b11RetranscriptionCrosscheckStatus: b11Retranscription?.status ?? null,
      b11PriorAuditArtifacts: b11PriorAudit?.artifact_count ?? 0,
      b11DirectPrintedNodePinHypotheses: b11PinPrefilter?.tested_hypothesis_count ?? 0,
      b11DirectPrintedNodeSoftCandidates: b11PinPrefilter?.soft_active_candidate_count ?? 0,
      b11D1D2PolarityPairs: d1d2Result?.candidate_count ?? 0,
      b11D1D2FunctionalPassCandidates: d1d2Result?.functional_pass_candidate_count ?? 0,
      b11D1D2AcceptedGuardedCandidate:
        d1d2Result?.accepted_for_future_guarded_mna_stamp ?? null,
      b11PassiveExperimentStampedResistors:
        enabledB11PassiveExperiment.result?.stamped_resistor_count ?? 0,
      b11PassiveExperimentExcludedComponents:
        enabledB11PassiveExperiment.result?.excluded_component_count ?? 0,
      b11PassiveCurrentConstraints: b11Requirements.length,
      b11PassiveLargestConstraintNode: b11RequirementSummary?.largest_constraint_node ?? null,
    },
    failures,
  };

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "u273_validation_report.json");
  fs.writeFileSync(filePath, JSON.stringify(report, null, 2));

  if (failures.length) {
    throw new Error(`Full validation failed: ${failures.join("; ")}`);
  }

  console.log("u273 full validation passed with guarded boundaries");
  console.log(`Wrote ${filePath}`);
}

if (require.main === module) {
  run();
}

module.exports = { run };
