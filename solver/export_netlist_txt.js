"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { buildNetlist } = require("./u273_orchestrator");

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function cell(value) {
  if (value === undefined || value === null) return "";
  if (typeof value === "number") return Number.isFinite(value) ? String(value) : "";
  if (typeof value === "object") return JSON.stringify(value).replaceAll("|", "/");
  return String(value).replaceAll("|", "/");
}

function componentNodeText(nodes) {
  if (!nodes) return "";
  return Object.entries(nodes)
    .map(([name, node]) => `${name}=${node}`)
    .join(";");
}

function renderText(netlist) {
  const lines = [];
  lines.push("Siemens U273 - netlist parametrique export texte");
  lines.push("====================================================");
  lines.push("");
  lines.push(`version: ${netlist.version}`);
  lines.push(`status: ${netlist.status}`);
  lines.push(`coupling_mode: ${netlist.coupling_mode}`);
  lines.push(`boundary: ${netlist.scientific_boundary}`);
  if (netlist.topology_step1?.debug_config) {
    const config = netlist.topology_step1.debug_config;
    lines.push(`debug_config: ${config.id}`);
    lines.push(`debug_config_status: ${config.status}`);
  }
  lines.push("");
  lines.push("Components");
  lines.push("id | type | value | unit | nodes | card | function | status | source_etape");

  for (const item of netlist.components) {
    lines.push([
      item.id,
      item.type,
      item.value,
      item.unit,
      componentNodeText(item.nodes),
      item.card,
      item.function,
      item.status,
      item.source_etape,
    ].map(cell).join(" | "));
  }

  lines.push("");
  lines.push("Switches");
  lines.push(`S6 status: ${netlist.switches.S6.status}`);
  lines.push(`S6 selected_position: ${netlist.switches.S6.selected_position}`);
  lines.push(`S6 truth_table_status: ${netlist.switches.S6.truth_table_status}`);
  lines.push(`S6 delivery_linear: ${netlist.switches.S6.delivery_linear_reading.contacts.join("; ")}`);
  lines.push(`S6 delivery_linear_status: ${netlist.switches.S6.delivery_linear_reading.status}`);
  lines.push(`S7 status: ${netlist.switches.S7.status}`);
  lines.push(`S7 selected_mode: ${netlist.switches.S7.selected_mode}`);
  lines.push(`S7 truth_table_status: ${netlist.switches.S7.truth_table_status}`);
  for (const entry of netlist.switches.S7.contact_table) {
    lines.push(`S7 ${entry.position}: closed=${entry.closed}, open=${entry.open}, zener=${entry.zenerPath}, mode=${entry.inferredMode}`);
  }

  const contactCandidateSet = netlist.topology_step1?.switch_contact_candidates;
  if (contactCandidateSet?.contacts?.length) {
    lines.push("");
    lines.push("Topology Step 1 switch contact candidates");
    lines.push(`candidate_set: ${contactCandidateSet.id}`);
    lines.push(`candidate_set_status: ${contactCandidateSet.status}`);
    lines.push(`candidate_set_truth_table_status: ${contactCandidateSet.truth_table_status}`);
    lines.push(`applied_to_executable_netlist: ${contactCandidateSet.applied_to_executable_netlist}`);
    for (const entry of contactCandidateSet.contacts) {
      lines.push(
        `${entry.id}: switch=${entry.switch}, state=${entry.contact_state}, terminals=${entry.terminals.join("-")}, ` +
        `status=${entry.status}, mna_action=${entry.mna_action}`
      );
    }
  }

  const switchMatrix = netlist.topology_step1?.b11_s6_s7_switch_matrix_candidate;
  if (switchMatrix) {
    lines.push("");
    lines.push("Topology Step 1 B11 S6/S7 switch matrix candidate");
    lines.push(`switch_matrix: ${switchMatrix.id}`);
    lines.push(`switch_matrix_status: ${switchMatrix.switch_matrix_status}`);
    lines.push(`switch_matrix_truth_table_status: ${switchMatrix.truth_table_status}`);
    lines.push(`switch_matrix_executable_effect: ${switchMatrix.executable_netlist_effect}`);
    lines.push(`switch_matrix_values: ${JSON.stringify(switchMatrix.known_network_values)}`);
    lines.push(`switch_matrix_forbidden_shortcuts: ${(switchMatrix.forbidden_shortcuts || []).join("; ")}`);
    lines.push(`switch_matrix_required_fields: ${(switchMatrix.per_position_required_fields || []).join("; ")}`);
  }

  const b11Inventory = netlist.topology_step1?.b11_visual_inventory;
  if (b11Inventory) {
    lines.push("");
    lines.push("Topology Step 1 B11 visual inventory");
    lines.push(`b11_inventory: ${b11Inventory.id}`);
    lines.push(`b11_inventory_status: ${b11Inventory.status}`);
    lines.push(`b11_inventory_mna_action: ${b11Inventory.mna_action}`);
    lines.push(`b11_active_simulation_allowed_for_step1: ${b11Inventory.active_simulation_allowed_for_step1}`);
    for (const entry of b11Inventory.topology_candidates || []) {
      lines.push(
        `${entry.item}: visible=${entry.visible_connection}, proposed=${entry.proposed_dc_stamp}, ` +
        `status=${entry.status}, source_status=${entry.source_status}, mna_action=${entry.mna_action}`
      );
    }
    if (b11Inventory.local_dc_checkpoints) {
      const checkpoint = b11Inventory.local_dc_checkpoints;
      lines.push(
        `${checkpoint.id}: status=${checkpoint.status}, ` +
        `iR15=${checkpoint.currents_amp.iR15}, iR16=${checkpoint.currents_amp.iR16}, ` +
        `n9PassiveDemand=${checkpoint.currents_amp.n9PassiveDemandAmp}`
      );
    }
  }

  const b11LocalLedger = netlist.topology_step1?.b11_local_topology_proof_ledger;
  if (b11LocalLedger) {
    const kcl = b11LocalLedger.local_kcl_checkpoint;
    lines.push("");
    lines.push("Topology Step 1 B11 local topology proof ledger");
    lines.push(`b11_local_ledger: ${b11LocalLedger.id}`);
    lines.push(`b11_local_ledger_status: ${b11LocalLedger.status}`);
    lines.push(`b11_local_ledger_mna_action: ${b11LocalLedger.mna_action}`);
    lines.push(
      `b11_local_R15_R16_R13: R15=${b11LocalLedger.canonical_value_reading_ohm.R15}, ` +
      `R16=${b11LocalLedger.canonical_value_reading_ohm.R16}, R13=${b11LocalLedger.canonical_value_reading_ohm.R13}`
    );
    lines.push(
      `b11_local_kcl: IR15=${kcl.iR15_from_V24_to_N145_amp}, ` +
      `IR16=${kcl.iR16_from_N9_to_reference_amp}, IR13=${kcl.iR13_from_N9_to_N05_amp}, ` +
      `residual=${kcl.residual_iR15_minus_iR16_minus_iR13_amp}, closure=${kcl.closure_status}`
    );
    for (const entry of b11LocalLedger.route_proof_table || []) {
      lines.push(
        `${entry.node}: touches=${(entry.components_touching_candidate || []).join("|")}, ` +
        `confidence=${entry.confidence}, must_prove=${entry.must_prove}`
      );
    }
  }

  const b11PdfEvidence = netlist.topology_step1?.b11_pdf_topology_evidence_ledger;
  if (b11PdfEvidence) {
    lines.push("");
    lines.push("Topology Step 1 B11 PDF topology evidence ledger");
    lines.push(`b11_pdf_evidence: ${b11PdfEvidence.id}`);
    lines.push(`b11_pdf_evidence_status: ${b11PdfEvidence.status}`);
    lines.push(`b11_pdf_evidence_mna_action: ${b11PdfEvidence.mna_action}`);
    lines.push(`b11_pdf_evidence_source: ${b11PdfEvidence.source_pdf}`);
    for (const entry of b11PdfEvidence.proof_rows || []) {
      const route = entry.visual_route ??
        (entry.terminal_graph
          ? Object.entries(entry.terminal_graph).map(([key, value]) => `${key}=${value}`).join("; ")
          : "n/a");
      lines.push(
        `${entry.component}: status=${entry.status}, route=${route}, ` +
        `limit=${entry.promotion_limit}`
      );
    }
    if (b11PdfEvidence.closure_verdict) {
      lines.push(
        `b11_pdf_closure: active_b11_promotable=${b11PdfEvidence.closure_verdict.active_b11_promotable}, ` +
        `replace_thevenin=${b11PdfEvidence.closure_verdict.replace_thevenin_b11_s6_s7_cmd}, ` +
        `still_guarded=${(b11PdfEvidence.closure_verdict.still_guarded || []).join("|")}`
      );
    }
  }

  const b11PdfTextFunctional = netlist.topology_step1?.b11_pdf_text_functional_evidence;
  if (b11PdfTextFunctional) {
    lines.push("");
    lines.push("Topology Step 1 B11 PDF text functional evidence");
    lines.push(`b11_pdf_text_functional: ${b11PdfTextFunctional.id}`);
    lines.push(`b11_pdf_text_functional_status: ${b11PdfTextFunctional.status}`);
    lines.push(`b11_pdf_text_functional_mna_action: ${b11PdfTextFunctional.mna_action}`);
    lines.push(`b11_pdf_text_functional_source: ${b11PdfTextFunctional.source_text_file}`);
    for (const entry of b11PdfTextFunctional.confirmed_functional_facts || []) {
      lines.push(
        `${entry.id}: fact=${entry.fact}, modeling=${entry.modeling_consequence}`
      );
    }
    if (b11PdfTextFunctional.empirical_diode_branch_law) {
      const law = b11PdfTextFunctional.empirical_diode_branch_law;
      lines.push(
        `b11_pdf_text_diode_law: scope=${law.scope}, voltage=${law.voltage_formula}, ` +
        `current=${law.current_formula}, target_mV=${JSON.stringify(law.bridge_signal_target_mV)}`
      );
    }
    if (b11PdfTextFunctional.closure_verdict) {
      lines.push(
        `b11_pdf_text_closure: active_b11_promotable=${b11PdfTextFunctional.closure_verdict.active_b11_promotable}, ` +
        `replace_thevenin=${b11PdfTextFunctional.closure_verdict.replace_thevenin_b11_s6_s7_cmd}, ` +
        `s7_truth_table_promoted=${b11PdfTextFunctional.closure_verdict.s7_contact_truth_table_promoted}`
      );
    }
  }

  const b11GptReponse2Audit = netlist.topology_step1?.b11_gpt_reponse_2_promotion_audit;
  if (b11GptReponse2Audit) {
    lines.push("");
    lines.push("Topology Step 1 B11 GPT reponse 2 promotion audit");
    lines.push(`b11_gpt_reponse_2_audit: ${b11GptReponse2Audit.id}`);
    lines.push(`b11_gpt_reponse_2_audit_status: ${b11GptReponse2Audit.status}`);
    lines.push(`b11_gpt_reponse_2_audit_mna_action: ${b11GptReponse2Audit.mna_action}`);
    lines.push(`b11_gpt_reponse_2_audit_source: ${b11GptReponse2Audit.source_file}`);
    for (const entry of b11GptReponse2Audit.claims_accepted_as_guarded_constraints || []) {
      lines.push(`${entry.claim}: guarded_action=${entry.project_action}`);
    }
    for (const entry of b11GptReponse2Audit.claims_rejected_or_not_promotable || []) {
      lines.push(`${entry.claim}: verdict=${entry.project_verdict}, reason=${entry.reason}`);
    }
    if (b11GptReponse2Audit.closure_verdict) {
      lines.push(
        `b11_gpt_reponse_2_closure: active_b11_promotable=${b11GptReponse2Audit.closure_verdict.active_b11_promotable}, ` +
        `add_official_components=${b11GptReponse2Audit.closure_verdict.add_official_b11_components_now}, ` +
        `c7_to_v24_rejected=${b11GptReponse2Audit.closure_verdict.c7_to_v24_rejected}`
      );
    }
  }

  const b11ScientificResearch = netlist.topology_step1?.b11_scientific_activation_research_ledger;
  if (b11ScientificResearch) {
    lines.push("");
    lines.push("Topology Step 1 B11 scientific activation research ledger");
    lines.push(`b11_scientific_research: ${b11ScientificResearch.id}`);
    lines.push(`b11_scientific_research_status: ${b11ScientificResearch.status}`);
    lines.push(`b11_scientific_research_mna_action: ${b11ScientificResearch.mna_action}`);
    lines.push(`b11_scientific_research_active_allowed: ${b11ScientificResearch.active_simulation_allowed}`);
    for (const source of b11ScientificResearch.scientific_sources || []) {
      lines.push(
        `${source.id}: kind=${source.kind}, consequence=${source.project_consequence}`
      );
    }
    for (const stamp of b11ScientificResearch.required_stamps_before_b11_activation || []) {
      lines.push(
        `${stamp.device}: current_status=${stamp.current_project_status || "required"}, ` +
        `missing=${stamp.missing_for_b11 || stamp.activation_gate || "none"}`
      );
    }
    for (const component of b11ScientificResearch.component_data_ledger || []) {
      lines.push(
        `${component.component}: source_strength=${component.source_strength}, ` +
        `promotion_allowed_now=${component.promotion_allowed_now}, gap=${component.data_gap}`
      );
    }
    lines.push(
      `b11_scientific_research_closure: zener_required=` +
      `${b11ScientificResearch.closure_verdict.zener_model_required_before_d1_stamp}, ` +
      `ebers_moll_dc_ok=${b11ScientificResearch.closure_verdict.ebers_moll_dc_is_sufficient_for_first_active_dc_experiment}, ` +
      `active_b11_promotable=${b11ScientificResearch.closure_verdict.active_b11_promotable}`
    );
  }

  const b11PdfActiveConstraints = netlist.topology_step1?.b11_pdf_active_hypothesis_constraints;
  if (b11PdfActiveConstraints) {
    lines.push("");
    lines.push("Topology Step 1 B11 PDF active hypothesis constraints");
    lines.push(`b11_pdf_active_constraints: ${b11PdfActiveConstraints.id}`);
    lines.push(`b11_pdf_active_constraints_status: ${b11PdfActiveConstraints.status}`);
    lines.push(`b11_pdf_active_constraints_mna_action: ${b11PdfActiveConstraints.mna_action}`);
    for (const entry of b11PdfActiveConstraints.current_constraints || []) {
      lines.push(
        `${entry.node}: action=${entry.required_action}, ` +
        `required_into_node=${entry.required_active_current_into_node_amp}, ` +
        `candidates=${(entry.likely_local_device_candidates || []).join("|")}, ` +
        `rule=${entry.rejection_rule}`
      );
    }
    lines.push(`b11_pdf_active_constraints_verdict: ${b11PdfActiveConstraints.closure_verdict}`);
  }

  const b11Retranscription = netlist.topology_step1?.b11_retranscription_crosscheck;
  if (b11Retranscription) {
    lines.push("");
    lines.push("Topology Step 1 B11 UTF-8 retranscription crosscheck");
    lines.push(`b11_retranscription_crosscheck: ${b11Retranscription.id}`);
    lines.push(`b11_retranscription_status: ${b11Retranscription.status}`);
    lines.push(`b11_retranscription_mna_action: ${b11Retranscription.mna_action}`);
    lines.push(`b11_retranscription_source: ${b11Retranscription.source_file}`);
    for (const entry of b11Retranscription.agrees_with_pdf_ledger || []) {
      lines.push(
        `${entry.item}: agrees=${entry.retranscription}, project_status=${entry.project_status}`
      );
    }
    for (const entry of b11Retranscription.useful_new_checklist_items || []) {
      lines.push(
        `${entry.item}: checklist_action=${entry.action}`
      );
    }
    lines.push(`b11_retranscription_verdict: ${b11Retranscription.closure_verdict}`);
  }

  const b11PriorAudit = netlist.topology_step1?.b11_prior_artifact_audit;
  if (b11PriorAudit) {
    lines.push("");
    lines.push("Topology Step 1 B11 prior artifact audit");
    lines.push(`b11_prior_audit: ${b11PriorAudit.id}`);
    lines.push(`b11_prior_audit_status: ${b11PriorAudit.status}`);
    lines.push(`b11_prior_audit_artifact_count: ${b11PriorAudit.artifact_count}`);
    for (const entry of b11PriorAudit.artifacts || []) {
      lines.push(
        `${entry.artifact}: status=${entry.status}, issue=${entry.issue_class}, ` +
        `allowed_use=${entry.allowed_use}`
      );
    }
  }

  const b11PinPrefilter = netlist.topology_step1?.b11_direct_printed_node_pin_prefilter;
  if (b11PinPrefilter) {
    lines.push("");
    lines.push("Topology Step 1 B11 direct printed-node pin prefilter");
    lines.push(`b11_pin_prefilter: ${b11PinPrefilter.id}`);
    lines.push(`b11_pin_prefilter_status: ${b11PinPrefilter.status}`);
    lines.push(`b11_pin_prefilter_mna_action: ${b11PinPrefilter.mna_action}`);
    lines.push(`b11_pin_prefilter_tested_hypotheses: ${b11PinPrefilter.tested_hypothesis_count}`);
    lines.push(`b11_pin_prefilter_soft_candidates: ${b11PinPrefilter.soft_active_candidate_count}`);
    lines.push(`b11_pin_prefilter_strict_candidates: ${b11PinPrefilter.strict_active_candidate_count}`);
    lines.push(`b11_pin_prefilter_conclusion: ${b11PinPrefilter.conclusion}`);
  }

  const b11D1D2Experiment = netlist.topology_step1?.b11_d1_d2_polarity_experiment;
  if (b11D1D2Experiment) {
    lines.push("");
    lines.push("Topology Step 1 B11 D1/D2 polarity experiment");
    lines.push(`b11_d1_d2_experiment: ${b11D1D2Experiment.id}`);
    lines.push(`b11_d1_d2_experiment_enabled: ${b11D1D2Experiment.enabled}`);
    lines.push(`b11_d1_d2_experiment_flag: ${b11D1D2Experiment.explicit_enable_flag}`);
    lines.push(`b11_d1_d2_experiment_scope: ${b11D1D2Experiment.allowed_scope}`);
    lines.push(`b11_d1_d2_experiment_isolated: ${b11D1D2Experiment.isolated_from_executable_netlist}`);
    lines.push(`b11_d1_d2_experiment_modifies_official_components: ${b11D1D2Experiment.modifies_official_components}`);
    lines.push(`b11_d1_d2_experiment_ncmd_local: ${b11D1D2Experiment.b11_ncmd_local_separate_from_b6_cmd}`);
    if (b11D1D2Experiment.result) {
      lines.push(`b11_d1_d2_candidate_pairs: ${b11D1D2Experiment.result.candidate_count}`);
      lines.push(`b11_d1_d2_functional_passes: ${b11D1D2Experiment.result.functional_pass_candidate_count}`);
      lines.push(`b11_d1_d2_guarded_candidate: ${b11D1D2Experiment.result.accepted_for_future_guarded_mna_stamp}`);
      for (const row of b11D1D2Experiment.result.candidate_rows || []) {
        lines.push(
          `${row.id}: status=${row.status}, limiter_vpk=${row.limiterThresholdPeakVolt}, ` +
          `compressor_vpk=${row.compressorThresholdPeakVolt}, promoted=${row.spice_polarity_promoted}`
        );
      }
    }
  }

  const b11PassiveCandidate = netlist.topology_step1?.b11_passive_candidate_subcircuit;
  if (b11PassiveCandidate) {
    lines.push("");
    lines.push("Topology Step 1 B11 passive candidate subcircuit");
    lines.push(`b11_passive_candidate: ${b11PassiveCandidate.id}`);
    lines.push(`b11_passive_candidate_status: ${b11PassiveCandidate.status}`);
    lines.push(`b11_passive_candidate_enabled: ${b11PassiveCandidate.enabled_in_executable_netlist}`);
    lines.push(`b11_passive_candidate_stamp_policy: ${b11PassiveCandidate.stamp_policy}`);
    lines.push(`b11_passive_candidate_component_count: ${b11PassiveCandidate.candidate_component_count}`);
    for (const entry of b11PassiveCandidate.candidate_components || []) {
      lines.push(
        `${entry.id}: type=${entry.type}, value=${entry.value}, nodes=${componentNodeText(entry.nodes)}, ` +
        `status=${entry.status}, mna_action=${entry.mna_action}`
      );
    }
  }

  if (netlist.topology_step1?.local_dc_checkpoints?.length) {
    lines.push("");
    lines.push("Topology Step 1 local DC checkpoints");
    for (const checkpoint of netlist.topology_step1.local_dc_checkpoints) {
      lines.push(`${checkpoint.id}: status=${checkpoint.status}, residual_amp=${checkpoint.kcl_residual_amp}`);
    }
  }

  const b11PassiveExperiment = netlist.topology_step1?.b11_passive_candidate_experiment;
  if (b11PassiveExperiment) {
    lines.push("");
    lines.push("Topology Step 1 B11 passive candidate experimental probe");
    lines.push(`b11_passive_experiment: ${b11PassiveExperiment.id}`);
    lines.push(`b11_passive_experiment_enabled: ${b11PassiveExperiment.enabled}`);
    lines.push(`b11_passive_experiment_flag: ${b11PassiveExperiment.explicit_enable_flag}`);
    lines.push(`b11_passive_experiment_scope: ${b11PassiveExperiment.allowed_scope}`);
    lines.push(`b11_passive_experiment_isolated: ${b11PassiveExperiment.isolated_from_executable_netlist}`);
    lines.push(`b11_passive_experiment_modifies_official_components: ${b11PassiveExperiment.modifies_official_components}`);
    if (b11PassiveExperiment.result) {
      lines.push(`b11_passive_experiment_stamped_resistors: ${b11PassiveExperiment.result.stamped_resistor_count}`);
      lines.push(`b11_passive_experiment_excluded_components: ${b11PassiveExperiment.result.excluded_component_count}`);
      lines.push(`b11_passive_experiment_dc_converged: ${b11PassiveExperiment.result.isolated_dc_solve.converged}`);
      lines.push(`b11_passive_experiment_dc_residual_max_abs: ${b11PassiveExperiment.result.isolated_dc_solve.residual_max_abs}`);
      for (const row of b11PassiveExperiment.result.node_current_balance_amp || []) {
        lines.push(
          `${row.node}: V=${row.printed_voltage_volt}, passive_leaving_A=${row.passive_current_leaving_node_amp}, ` +
          `required_external_into_node_A=${row.external_current_required_into_node_amp}`
        );
      }
      const summary = b11PassiveExperiment.result.active_current_requirement_summary;
      if (summary) {
        lines.push(
          `b11_passive_experiment_current_constraints: count=${summary.constraint_count}, ` +
          `largest=${summary.largest_constraint_node}, largest_abs_A=${summary.largest_constraint_abs_amp}`
        );
      }
      for (const row of b11PassiveExperiment.result.active_current_requirements || []) {
        lines.push(
          `${row.node}: action=${row.required_action}, abs_A=${row.magnitude_abs_amp}, status=${row.status}`
        );
      }
    }
  }

  lines.push("");
  lines.push("Open scientific boundaries");
  lines.push("- B11 active regulator is not yet a direct MNA block.");
  lines.push("- B6 bridge diode law is closed, but final polarities remain photo-dependent.");
  lines.push("- B6 Ts1/Ts3/Ts5/Ts6 and B11 Ts1/Ts2 stay symbolic or unconfirmed until route proof.");
  lines.push("- S6 non-delivery positions and S1..S5 are not final contact tables.");
  lines.push("");

  return `${lines.join("\n")}\n`;
}

function exportTxt(options = {}) {
  const netlist = buildNetlist(options);
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);
  const filePath = path.join(outDir, "u273_netlist.txt");
  fs.writeFileSync(filePath, renderText(netlist));
  return { filePath, netlist };
}

function run() {
  const { filePath, netlist } = exportTxt();
  console.log(`Wrote ${filePath}`);
  console.log(`${netlist.components.length} components, ${netlist.nodes.length} non-ground nodes`);
}

if (require.main === module) {
  run();
}

module.exports = {
  renderText,
  exportTxt,
  run,
};
