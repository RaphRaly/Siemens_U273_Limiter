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
  UNCONFIRMED: "UNCONFIRMED",
  NON_READ: "NON_LU",
  NUMERICAL: "NUMERICAL_HELPER",
});

const DEFAULTS = Object.freeze({
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
    couplingMode: options.couplingMode ?? DEFAULTS.couplingMode,
    s6Position: options.s6Position ?? DEFAULTS.s6Position,
    s7Mode: options.s7Mode ?? DEFAULTS.s7Mode,
    commandSourceVolt,
    b6,
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
      s6_position: resolved.s6Position,
      s7_mode: resolved.s7Mode,
      command_source_volt: resolved.commandSourceVolt,
      command_source_ohm: resolved.b6.commandSourceOhm,
    },
    parameters: {
      b6: Object.fromEntries(Object.entries(params).map(([key, value]) => [key, finiteOrString(value)])),
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
        delivery_linear_reading: {
          status: STATUS.CLOSED,
          contacts: ["4-5 closed", "8-9 closed", "5-6 open", "7-8 open", "5-3-1 closed", "2 open"],
          source_etape: "Etape 20/21/24",
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
        contact_table: s7ContactTable,
        detector_scenarios: detectorScenarios.map((scenario) => ({
          name: scenario.name,
          mode: scenario.mode,
          s7_position: scenario.s7Position,
          s7_closed: scenario.s7Closed,
          zener_path: scenario.zenerPath,
          status: scenario.mode === "limiter" ? STATUS.CLOSED : STATUS.HYPOTHESIS,
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
  buildDcNetlist,
  buildNetlist,
  solveOperatingPoint,
  writeSnapshot,
  run,
};
