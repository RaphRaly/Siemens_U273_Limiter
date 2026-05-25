"use strict";

// Parametric B6 bridge model used by many exploratory scripts. Component values
// are kept centralized so DC/AC/transient sweeps share the same assumptions.
const { c } = require("./complex_ac");

const TWO_PI = 2 * Math.PI;
const OHM = 1;
const K = 1e3;

const defaultB6Values = Object.freeze({
  r1: 5.1 * K,
  r2: 5.1 * K,
  r3Total: 1 * K,
  r4Total: 200 * OHM,
  r5: 5.6 * K,
  r6: 39 * K,
  r7Total: 100 * OHM,
  r8Total: 250 * K,
  r9: 390 * K,
  r10: 20 * K,
  r11: 39 * K,
  c1: 0.68e-6,
  c2: 22e-6,
  c3: 4.7e-6,
  c4Abgl: 0,
  c5: 150e-6,
  c6Abgl: 0,
  c7: 4.7e-6,
  r12: 51 * K,
  r13: 5.6 * K,
  r14: 8.2 * K,
  r15: 30 * K,
  r16Total: 500 * OHM,
  r17: 6.8 * K,
  r18: 68 * K,
  r19: 30 * K,
  r20: 1.2 * K,
  r21: 10 * K,
  r22: 8.2 * K,
  r23: 33 * K,
  r24: 9.1 * K,
  r25: 16 * K,
  r26: 220 * OHM,
  r27: 620 * OHM,
  r28: 10 * K,
  r29: 20 * K,
  r30: 6.8 * K,
  r31: 5.6 * K,
  r32: 1 * K,
  r33: 20 * K,
  r34: 3.6 * K,
  r35: 820 * OHM,
  r36: 24 * OHM,
  r37: 10 * OHM,
  r38: 68 * OHM,
  c8: 10e-6,
  c9: 50e-6,
  c10: 25e-6,
  c11: 17e-9,
  c12: 2e-9,
  c13: 25e-6,
  c14: 70e-6,
  c15: 150e-6,
  c16: 100e-9,
  c17: 100e-6,
  c18: 50e-6,
  c19: 220e-6,
  c20: 50e-6,
  c21: 500e-6,
  gminResistance: 1e12,
  rAmpInput: Infinity,
  zcmd: 0,
  commandSourceOhm: 0,
});

const B6_STATUS = Object.freeze({
  CLOSED: "FERME_SCHEMA",
  PARTIAL: "PARTIEL",
  PARAMETRIC: "PARAMETRIQUE",
  GUARDED: "GARDE_NON_STAMPE",
  SWITCH: "COMMUTATEUR_SCHEMA",
});

const diodeModels = Object.freeze({
  SSD55: Object.freeze({ is: 1e-12, n: 1.7, vt: 25.852e-3 }),
  OA154Q: Object.freeze({ is: 1e-8, n: 1.6, vt: 25.852e-3 }),
  U273_EMPIRICAL: Object.freeze({
    model: "u273_empirical",
    reverseGmin: 1e-12,
    maxVd: 1.2,
  }),
});

function assertFinitePositive(name, value) {
  if (!Number.isFinite(value) || value <= 0) {
    throw new Error(`${name} must be a finite positive number`);
  }
}

function assertNonNegative(name, value) {
  if (!Number.isFinite(value) || value < 0) {
    throw new Error(`${name} must be a finite non-negative number`);
  }
}

function clampUnit(name, value) {
  if (!Number.isFinite(value) || value < 0 || value > 1) {
    throw new Error(`${name} must be between 0 and 1`);
  }
  return value;
}

function effectiveAdjustableResistance(name, total, effectiveValue, alpha, minValue) {
  if (effectiveValue !== undefined) {
    assertFinitePositive(`${name}Effective`, effectiveValue);
    return effectiveValue;
  }
  if (alpha !== undefined) {
    const a = clampUnit(`alpha${name}`, alpha);
    return Math.max(minValue, total * a);
  }
  return total;
}

function resolveB6Params(options = {}) {
  const v = defaultB6Values;
  const r7 = effectiveAdjustableResistance("7", v.r7Total, options.r7EffectiveOhm, options.alpha7, options.r7MinOhm ?? 1e-3);
  const r8 = effectiveAdjustableResistance("8", v.r8Total, options.r8EffectiveOhm, options.alpha8, options.r8MinOhm ?? 1);
  const c4Abgl = options.c4AbglFarad ?? v.c4Abgl;
  const c6Abgl = options.c6AbglFarad ?? v.c6Abgl;
  const rAmpInput = options.rAmpInputOhm ?? v.rAmpInput;
  const zcmd = options.zcmdOhm ?? v.zcmd;
  const commandDcMode = options.commandDcMode ?? "ideal";
  const commandSourceOhm = options.commandSourceOhm ?? v.commandSourceOhm;

  assertFinitePositive("r7", r7);
  assertFinitePositive("r8", r8);
  assertNonNegative("c4AbglFarad", c4Abgl);
  assertNonNegative("c6AbglFarad", c6Abgl);
  if (!(rAmpInput === Infinity)) assertFinitePositive("rAmpInputOhm", rAmpInput);
  if (!(zcmd === Infinity)) assertNonNegative("zcmdOhm", zcmd);
  if (!(commandDcMode === "ideal" || commandDcMode === "thevenin")) {
    throw new Error("commandDcMode must be ideal or thevenin");
  }
  if (commandDcMode === "thevenin") {
    assertFinitePositive("commandSourceOhm", commandSourceOhm);
  } else {
    assertNonNegative("commandSourceOhm", commandSourceOhm);
  }

  return {
    ...v,
    r7,
    r8,
    c4Abgl,
    c6Abgl,
    rAmpInput,
    zcmd,
    bridgeDiodeLaw: options.bridgeDiodeLaw ?? "shockley_placeholder",
    commandDcMode,
    commandSourceOhm,
    commandAcMode: options.commandAcMode ?? (zcmd === 0 ? "ideal" : "finite"),
    adjustmentModel: "R7/R8 are effective two-terminal resistances unless alpha7/alpha8 is explicitly supplied as a rheostat simplification.",
  };
}

function resolveBridgeDiodeModel(type, options = {}) {
  if (options.bridgeDiodeLaw === "u273_empirical") {
    return {
      ...diodeModels.U273_EMPIRICAL,
      empiricalLawNote:
        "Siemens U273 measured bridge diode power law; applied to each D1-D4 schematic diode element.",
      nominalType: type,
    };
  }
  return diodeModels[type];
}

function yRes(value) {
  assertFinitePositive("resistance", value);
  return c(1 / value, 0);
}

function yCap(frequency, value) {
  assertNonNegative("capacitance", value);
  return c(0, TWO_PI * frequency * value);
}

function addAdmittance(admittances, name, n1, n2, y) {
  if (Math.abs(y.re) < 1e-30 && Math.abs(y.im) < 1e-30) return;
  admittances.push({ name, n1, n2, y });
}

function schematicComponent(id, type, value, unit, nodes, fields = {}) {
  return {
    id,
    type,
    value: value === undefined ? null : value,
    unit: unit ?? null,
    nodes,
    card: "B6",
    status: fields.status ?? B6_STATUS.CLOSED,
    source_etape: fields.source_etape ?? "Etape 26",
    ...fields,
  };
}

function buildB6CompleteSchematicInventory(options = {}) {
  const p = resolveB6Params(options);
  const parts = [];
  const add = (id, type, value, unit, nodes, fields = {}) =>
    parts.push(schematicComponent(id, type, value, unit, nodes, fields));

  add("B6.S1", "switch", null, null, { common: "B6_IN_H", positions: ["2", "6"], out: "B6_IN_H_SW" }, {
    status: B6_STATUS.SWITCH,
    function: "input range/source switch upper leg",
  });
  add("B6.S2", "switch", null, null, { common: "B6_IN_L", positions: ["1", "8"], out: "B6_IN_L_SW" }, {
    status: B6_STATUS.SWITCH,
    function: "input range/source switch lower leg",
  });
  add("B6.R1", "resistor", p.r1, "ohm", { n1: "B6_IN_H_SW", n2: "U1_PRI_H" }, { function: "input series resistor" });
  add("B6.R2", "resistor", p.r2, "ohm", { n1: "B6_IN_L_SW", n2: "U1_PRI_L" }, { function: "input series resistor" });
  add("B6.R3", "potentiometer", p.r3Total, "ohm", { end1: "U1_PRI_H", wiper: "S5_TOP", end2: "S5_BUS" }, {
    status: B6_STATUS.PARAMETRIC,
    function: "input trim",
  });
  add("B6.R4", "potentiometer", p.r4Total, "ohm", { end1: "U1_PRI_H", wiper: "S5_BOTTOM", end2: "S5_BUS" }, {
    status: B6_STATUS.PARAMETRIC,
    function: "input trim",
  });
  add("B6.S5", "switch", null, null, { common: "S5_BUS", contacts: ["8", "9", "10"] }, {
    status: B6_STATUS.SWITCH,
    function: "input calibration/range switch",
  });
  add("B6.U1", "transformer", null, null, { primary: "U1_PRI_H/U1_PRI_L", secondary: "U1_SEC_H/U1_SEC_L" }, {
    function: "input transformer",
    winding_labels: "rt/bl/ge/sw as printed",
  });

  add("B6.R5", "resistor", p.r5, "ohm", { n1: "U1_SEC_H", n2: "NX" }, { function: "bridge input feed" });
  add("B6.C1", "capacitor", p.c1, "farad", { n1: "NX", n2: "NA_25MV" }, {
    function: "bridge input coupling; corrected from provisional 1000p to schematic 0.68u",
  });
  add("B6.C2", "capacitor", p.c2, "farad", { n1: "NA_25MV", n2: "NB" }, { function: "bridge feed coupling" });
  add("B6.R9", "resistor", p.r9, "ohm", { n1: "NB", n2: "D2_D1_RIGHT_RAIL" }, { function: "bridge bias/feed" });
  add("B6.R7", "potentiometer", p.r7, "ohm", { end1: "D3_D4_LEFT_RAIL", wiper: "NB", end2: "D2_D1_RIGHT_RAIL" }, {
    status: B6_STATUS.PARAMETRIC,
    function: "bridge balance, effective value used in MNA",
  });
  add("B6.R8", "potentiometer", p.r8, "ohm", { end1: "N14", wiper: "R10_LEFT", end2: "N15" }, {
    status: B6_STATUS.PARAMETRIC,
    function: "bridge balance/command trim, effective value used in MNA",
  });
  add("B6.R10", "resistor", p.r10, "ohm", { n1: "CMD", n2: "R10_LEFT" }, { function: "B11/S7 command injection" });
  add("B6.D3", "diode", "SSD55", null, { anode: "NB", cathode: "NL" }, { function: "left upper bridge diode" });
  add("B6.D4", "diode", "OA154Q", null, { anode: "NL", cathode: "N14" }, { function: "left lower bridge diode" });
  add("B6.D2", "diode", "SSD55", null, { anode: "NB", cathode: "NR" }, { function: "right upper bridge diode" });
  add("B6.D1", "diode", "OA154Q", null, { anode: "NR", cathode: "N15" }, { function: "right lower bridge diode" });
  add("B6.R6", "resistor", p.r6, "ohm", { n1: "N14", n2: "0" }, { function: "left bridge return" });
  add("B6.C3", "capacitor", p.c3, "farad", { n1: "N14", n2: "0" }, { function: "left bridge bypass" });
  add("B6.C4", "capacitor_adjust", p.c4Abgl, "farad", { n1: "N14", n2: "0" }, {
    status: B6_STATUS.PARAMETRIC,
    function: "Abgl adjustment capacitor",
  });
  add("B6.C5", "capacitor", p.c5, "farad", { n1: "N14", n2: "N15" }, { function: "bridge coupling/storage" });
  add("B6.C6", "capacitor_adjust", p.c6Abgl, "farad", { n1: "N15", n2: "0" }, {
    status: B6_STATUS.PARAMETRIC,
    function: "Abgl adjustment capacitor",
  });
  add("B6.C7", "capacitor", p.c7, "farad", { n1: "N15", n2: "0" }, { function: "right bridge bypass" });
  add("B6.R11", "resistor", p.r11, "ohm", { n1: "N15", n2: "0" }, { function: "right bridge return" });

  add("B6.R12", "resistor", p.r12, "ohm", { n1: "V64", n2: "TS1_BIAS_TOP" }, { function: "Ts1 bias from 6.4V rail" });
  add("B6.R13", "resistor", p.r13, "ohm", { n1: "TS1_BIAS_TOP", n2: "N08" }, { function: "Ts1 bias network" });
  add("B6.R14", "resistor", p.r14, "ohm", { n1: "N08", n2: "0" }, { function: "Ts1 emitter/bias return" });
  add("B6.R15", "resistor", p.r15, "ohm", { n1: "V64", n2: "N38" }, { function: "Ts2 bias feed" });
  add("B6.R16", "potentiometer", p.r16Total, "ohm", { end1: "N08", wiper: "R16_WIPER", end2: "R17_TOP" }, {
    status: B6_STATUS.PARAMETRIC,
    function: "front-stage trim",
  });
  add("B6.R17", "resistor", p.r17, "ohm", { n1: "R17_TOP", n2: "0" }, { function: "front-stage trim return" });
  add("B6.R18", "resistor", p.r18, "ohm", { n1: "N38", n2: "N08" }, { function: "Ts2 bias feedback" });
  add("B6.R19", "resistor", p.r19, "ohm", { n1: "V64", n2: "V122" }, { function: "supply divider/feed" });
  add("B6.R20", "resistor", p.r20, "ohm", { n1: "N08", n2: "N074" }, { function: "Ts3 emitter/local feedback" });
  add("B6.R21", "resistor", p.r21, "ohm", { n1: "V122", n2: "V64" }, { function: "supply/bias feed" });
  add("B6.R22", "resistor", p.r22, "ohm", { n1: "N32", n2: "0" }, { function: "Ts2 emitter current reference" });
  add("B6.R23", "resistor", p.r23, "ohm", { n1: "V64", n2: "N24" }, { function: "Ts3 bias with C14" });
  add("B6.R24", "resistor", p.r24, "ohm", { n1: "N32", n2: "0" }, { function: "front-stage emitter/reference path" });
  add("B6.R25", "resistor", p.r25, "ohm", { n1: "V122", n2: "N24" }, { function: "Ts3 bias feed" });
  add("B6.R26", "resistor", p.r26, "ohm", { n1: "N074", n2: "R27_TOP" }, { function: "Ts3 emitter resistor chain" });
  add("B6.R27", "resistor", p.r27, "ohm", { n1: "R27_TOP", n2: "0" }, { function: "Ts3 emitter resistor chain" });
  add("B6.R28", "resistor", p.r28, "ohm", { n1: "N105", n2: "N18" }, { function: "Ts4/output driver bias" });
  add("B6.R29", "resistor", p.r29, "ohm", { n1: "N18", n2: "0" }, { function: "Ts4 emitter/load return" });
  add("B6.R30", "resistor", p.r30, "ohm", { n1: "V122", n2: "V22" }, { function: "supply feed to 22V rail" });
  add("B6.R31", "resistor", p.r31, "ohm", { n1: "V22", n2: "N48" }, { function: "Ts4 collector/load feed" });
  add("B6.R32", "resistor", p.r32, "ohm", { n1: "N18", n2: "0" }, { function: "Ts4 current reference" });
  add("B6.R33", "resistor", p.r33, "ohm", { n1: "OUTPUT_BIAS_LEFT", n2: "N215" }, {
    status: B6_STATUS.PARTIAL,
    function: "output-stage bias feedback from left bias rail to 21.5 V node",
  });
  add("B6.R34", "resistor", p.r34, "ohm", { n1: "OUTPUT_BIAS_LEFT", n2: "N105" }, {
    status: B6_STATUS.PARTIAL,
    function: "output-stage bias feed into Ts5/Ts6 midpoint",
  });
  add("B6.R35", "resistor", p.r35, "ohm", { n1: "OUTPUT_BIAS_LEFT", n2: "0" }, {
    status: B6_STATUS.PARTIAL,
    function: "output-stage lower bias return",
  });
  add("B6.R36", "resistor", p.r36, "ohm", { n1: "V22", n2: "N215" }, { function: "output idle-current sense/feed" });
  add("B6.R37", "resistor", p.r37, "ohm", { n1: "TS6_EMITTER", n2: "0" }, {
    status: B6_STATUS.PARTIAL,
    function: "output lower emitter resistor; Ts6 terminal proof still guarded",
  });
  add("B6.R38", "resistor", p.r38, "ohm", { n1: "V22", n2: "SUPPLY_SI_0A4" }, { function: "supply series resistor/fuse feed" });

  add("B6.C8", "capacitor", p.c8, "farad", { n1: "N08", n2: "TS1_BIAS_TOP" }, { function: "front-stage coupling/compensation" });
  add("B6.C9", "capacitor", p.c9, "farad", { n1: "N32", n2: "0" }, { function: "Ts2 emitter bypass" });
  add("B6.C10", "capacitor", p.c10, "farad", { n1: "V64", n2: "0" }, { function: "6.4V rail decoupling" });
  add("B6.C11", "capacitor", p.c11, "farad", { n1: "N38", n2: "N08" }, { function: "front-stage compensation" });
  add("B6.C12", "capacitor", p.c12, "farad", { n1: "N08", n2: "N074" }, { function: "Ts3 compensation" });
  add("B6.C13", "capacitor", p.c13, "farad", { n1: "N32", n2: "0" }, { function: "front-stage bypass" });
  add("B6.C14", "capacitor", p.c14, "farad", { n1: "N24", n2: "V64" }, { function: "Ts3 bias bypass with R23" });
  add("B6.C15", "capacitor", p.c15, "farad", { n1: "V122", n2: "0" }, { function: "12.2V rail decoupling" });
  add("B6.C16", "capacitor", p.c16, "farad", { n1: "N074", n2: "N18" }, { function: "Ts3/Ts4 coupling" });
  add("B6.C17", "capacitor", p.c17, "farad", { n1: "N18", n2: "0" }, { function: "Ts4 bypass" });
  add("B6.C18", "capacitor", p.c18, "farad", { n1: "N18", n2: "0" }, { function: "Ts4/output compensation" });
  add("B6.C19", "capacitor", p.c19, "farad", { n1: "V122", n2: "V22" }, { function: "22V feed decoupling" });
  add("B6.C20", "capacitor", p.c20, "farad", { n1: "OUTPUT_BIAS_LEFT", n2: "N215" }, {
    status: B6_STATUS.PARTIAL,
    function: "output-stage AC feedback/decoupling from left bias rail to 21.5 V node",
  });
  add("B6.C21", "capacitor", p.c21, "farad", { n1: "N105", n2: "U2_PRI_TOP" }, {
    status: B6_STATUS.PARTIAL,
    function: "series output coupling capacitor feeding U2 primary",
  });

  add("B6.Ts1", "bjt", "BCY66", null, { terminals: "from Siemens schematic symbol", printedNodes: ["V64", "N08"] }, {
    status: B6_STATUS.GUARDED,
    function: "front signal transistor; not nonlinear-stamped by default",
  });
  add("B6.Ts2", "bjt", "BCY66", null, { collector: "V64", base: "N38", emitter: "N32" }, {
    status: B6_STATUS.GUARDED,
    function: "front signal transistor; hybrid-pi candidate only",
  });
  add("B6.Ts3", "bjt", "SST117/1", null, { terminals: "from Siemens schematic symbol", printedNodes: ["N24", "N074"] }, {
    status: B6_STATUS.GUARDED,
    function: "middle gain/driver transistor; not nonlinear-stamped by default",
  });
  add("B6.Ts4", "bjt", "SST116/1", null, { collector: "N48", base: "N24", emitter: "N18" }, {
    status: B6_STATUS.GUARDED,
    function: "middle gain/driver transistor; hybrid-pi candidate only",
  });
  add("B6.Ts5", "bjt", "SST117/1", null, { terminals: "from Siemens schematic symbol", printedNodes: ["N215", "N105"] }, {
    status: B6_STATUS.GUARDED,
    function: "upper output transistor; output-stage bound only",
  });
  add("B6.Ts6", "bjt", "SST117/1", null, { terminals: "from Siemens schematic symbol", printedNodes: ["N105", "0"] }, {
    status: B6_STATUS.GUARDED,
    function: "lower output transistor; output-stage bound only",
  });
  add("B6.U2", "transformer", null, null, { primary: "U2_PRI_TOP/U2_PRI_BOTTOM", secondary: "B6_OUT_300R" }, {
    status: B6_STATUS.PARTIAL,
    function: "output transformer",
  });
  add("B6.S3", "switch", null, null, { contacts: ["22", "23"], output: "B6_OUT_PLUS" }, {
    status: B6_STATUS.SWITCH,
    function: "output switch upper section",
    contact_truth_status: "SWITCH_CONTACT_CANDIDATE",
    selected_config: "U273_DEBUG_CONFIG_001",
    contact_candidates: [
      {
        id: "S3_DRAWN_22_23_CLOSED",
        terminals: ["22", "23"],
        contact_state: "candidate_closed",
        status: "SWITCH_CONTACT_CANDIDATE",
        truth_table_status: "UNKNOWN",
        mna_action: "boundary_only_not_stamped",
      },
    ],
  });
  add("B6.S4", "switch", null, null, { contacts: ["25", "26"], output: "B6_OUT_RETURN" }, {
    status: B6_STATUS.SWITCH,
    function: "output switch lower section",
    contact_truth_status: "SWITCH_CONTACT_CANDIDATE",
    selected_config: "U273_DEBUG_CONFIG_001",
    contact_candidates: [
      {
        id: "S4_DRAWN_25_26_CLOSED",
        terminals: ["25", "26"],
        contact_state: "candidate_closed",
        status: "SWITCH_CONTACT_CANDIDATE",
        truth_table_status: "UNKNOWN",
        mna_action: "boundary_only_not_stamped",
      },
    ],
  });

  return {
    title: "Siemens U273 B6 complete schematic inventory",
    status: "INVENTAIRE_SCHEMA_AVEC_SORTIE_ACTIVE_PARTIELLE",
    source: "Etape 26",
    boundary:
      "Visible B6 service-sheet components are inventoried. The bridge/passive subset is usable, but the Ts5/Ts6/C21/U2 output stage remains partial and all active BJT nonlinear stamps stay guarded.",
    node_aliases: {
      NA_25MV: "printed 25 mV bridge input node",
      NB: "bridge command/input node below C2",
      N14: "left lower bridge node/contact 14",
      N15: "right lower bridge node/contact 15",
      CMD: "B11/S7 command injection node via R10",
      V64: "printed 6.4 V rail",
      V122: "printed 12.2 V rail",
      V22: "printed 22 V rail",
      N215: "printed 21.5 V output upper node",
      N105: "printed 10.5 V output midpoint",
      OUTPUT_BIAS_LEFT: "output-stage left bias rail feeding R33/C20/R34/R35",
    },
    components: parts,
  };
}

function buildB6ParametricDcNetlist(vcmd, options = {}) {
  const p = resolveB6Params(options);
  const commandSourceVolt = options.commandSourceVolt ?? vcmd;
  const voltageSources = [];
  const resistors = [
    { name: "R10", n1: "CMD", n2: "NB", value: p.r10 },
    { name: "R9", n1: "NB", n2: "0", value: p.r9 },
    { name: "R7_effective", n1: "NL", n2: "NR", value: p.r7 },
    { name: "R8_effective", n1: "N14", n2: "N15", value: p.r8 },
    { name: "R6", n1: "N14", n2: "0", value: p.r6 },
    { name: "R11", n1: "N15", n2: "0", value: p.r11 },
    { name: "GMIN_NL", n1: "NL", n2: "0", value: p.gminResistance },
    { name: "GMIN_NR", n1: "NR", n2: "0", value: p.gminResistance },
  ];

  let commandDcModel;
  if (p.commandDcMode === "ideal") {
    voltageSources.push({ name: "VCMD", nPlus: "CMD", nMinus: "0", value: vcmd });
    commandDcModel = "ideal imposed CMD voltage";
  } else {
    voltageSources.push({ name: "VB11_S6_S7", nPlus: "B11_DRV", nMinus: "0", value: commandSourceVolt });
    resistors.unshift({ name: "RB11_S6_S7_CMD", n1: "B11_DRV", n2: "CMD", value: p.commandSourceOhm });
    commandDcModel = `B11/S6/S7 Thevenin source ${commandSourceVolt} V through ${p.commandSourceOhm} Ohm`;
  }

  return {
    title: "Siemens U273 B6 parametric bridge DC netlist",
    notes: [
      p.commandDcMode === "ideal"
        ? "Local B6 operating point under imposed DC command voltage."
        : "Local B6 operating point driven by a finite B11/S6/S7 Thevenin command source.",
      "Capacitors are open for DC.",
      "C1 is corrected to the complete B6 schematic value 0.68 uF; it does not enter this DC solve.",
      "C4/C6 are adjustment capacitors and are open for DC.",
      p.bridgeDiodeLaw === "u273_empirical"
        ? "Bridge diodes use the Siemens empirical U_D/I_D/r_d law from the U273 notes."
        : "Diode parameters are placeholders until datasheet, I-V measurement, or calibration.",
    ],
    commandDcModel,
    parameters: p,
    voltageSources,
    resistors,
    diodes: [
      { name: "D3_SSD55", anode: "NB", cathode: "NL", ...resolveBridgeDiodeModel("SSD55", options) },
      { name: "D4_OA154Q", anode: "NL", cathode: "N14", ...resolveBridgeDiodeModel("OA154Q", options) },
      { name: "D2_SSD55", anode: "NB", cathode: "NR", ...resolveBridgeDiodeModel("SSD55", options) },
      { name: "D1_OA154Q", anode: "NR", cathode: "N15", ...resolveBridgeDiodeModel("OA154Q", options) },
    ],
  };
}

function diodeConductanceMap(diodeInfo) {
  return Object.fromEntries(diodeInfo.map((d) => [d.name, d.gd]));
}

function stampCommandPort(admittances, voltageSources, p) {
  if (p.commandAcMode === "ideal" || p.zcmd === 0) {
    voltageSources.push({ name: "VCMD_AC_GROUND", nPlus: "CMD", nMinus: "0", value: c(0, 0) });
    return "ideal AC ground";
  }
  if (p.commandAcMode === "open" || p.zcmd === Infinity) {
    return "open AC command port";
  }
  addAdmittance(admittances, "ZCMD", "CMD", "0", yRes(p.zcmd));
  return `finite command impedance ${p.zcmd} Ohm`;
}

function buildB6ParametricAcNetlist(frequency, diodeGd, options = {}) {
  const p = resolveB6Params(options);
  const admittances = [];
  const voltageSources = [
    { name: "VAC", nPlus: "VS", nMinus: "0", value: c(1, 0) },
  ];

  addAdmittance(admittances, "R5", "VS", "NX", yRes(p.r5));
  addAdmittance(admittances, "C1_1000p", "NX", "NA", yCap(frequency, p.c1));
  addAdmittance(admittances, "C2_22u", "NA", "NB", yCap(frequency, p.c2));
  addAdmittance(admittances, "R10_to_CMD", "NB", "CMD", yRes(p.r10));
  addAdmittance(admittances, "R9", "NB", "0", yRes(p.r9));
  addAdmittance(admittances, "R7_effective", "NL", "NR", yRes(p.r7));
  addAdmittance(admittances, "R8_effective", "N14", "N15", yRes(p.r8));
  addAdmittance(admittances, "R6", "N14", "0", yRes(p.r6));
  addAdmittance(admittances, "R11", "N15", "0", yRes(p.r11));
  addAdmittance(admittances, "D3_gd", "NB", "NL", c(diodeGd.D3_SSD55 ?? 0, 0));
  addAdmittance(admittances, "D4_gd", "NL", "N14", c(diodeGd.D4_OA154Q ?? 0, 0));
  addAdmittance(admittances, "D2_gd", "NB", "NR", c(diodeGd.D2_SSD55 ?? 0, 0));
  addAdmittance(admittances, "D1_gd", "NR", "N15", c(diodeGd.D1_OA154Q ?? 0, 0));
  addAdmittance(admittances, "C3_4u7", "N14", "0", yCap(frequency, p.c3));
  addAdmittance(admittances, "C4_abgl", "N14", "0", yCap(frequency, p.c4Abgl));
  addAdmittance(admittances, "C5_150u", "N14", "N15", yCap(frequency, p.c5));
  addAdmittance(admittances, "C6_abgl", "N15", "0", yCap(frequency, p.c6Abgl));
  addAdmittance(admittances, "C7_4u7", "N15", "0", yCap(frequency, p.c7));
  addAdmittance(admittances, "GMIN_NL", "NL", "0", yRes(p.gminResistance));
  addAdmittance(admittances, "GMIN_NR", "NR", "0", yRes(p.gminResistance));
  if (p.rAmpInput !== Infinity) {
    addAdmittance(admittances, "RAMP_INPUT", "NA", "0", yRes(p.rAmpInput));
  }

  const commandModel = stampCommandPort(admittances, voltageSources, p);

  return {
    title: "Siemens U273 B6 parametric small-signal AC netlist",
    frequency,
    commandModel,
    parameters: p,
    admittances,
    voltageSources,
  };
}

module.exports = {
  defaultB6Values,
  B6_STATUS,
  diodeModels,
  resolveB6Params,
  resolveBridgeDiodeModel,
  yRes,
  yCap,
  buildB6CompleteSchematicInventory,
  buildB6ParametricDcNetlist,
  buildB6ParametricAcNetlist,
  diodeConductanceMap,
};
