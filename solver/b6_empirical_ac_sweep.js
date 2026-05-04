"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { solveDc } = require("./mna_core");
const { c, abs, solveAc } = require("./complex_ac");
const {
  diodeCurrentMicroAmpFromVoltageMilliVolt,
  dynamicResistanceKiloOhmFromCurrentMicroAmp,
} = require("./u273_diode_empirical");
const {
  buildB6ParametricDcNetlist,
  buildB6ParametricAcNetlist,
  diodeConductanceMap,
} = require("./b6_parametric");

function db20(value) {
  if (value <= 0) return -Infinity;
  return 20 * Math.log10(value);
}

function phaseDeg(value) {
  return Math.atan2(value.im, value.re) * 180 / Math.PI;
}

function serializeComplex(value) {
  return { re: value.re, im: value.im, mag: abs(value), phaseDeg: phaseDeg(value) };
}

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function csvCell(value) {
  if (typeof value === "number") return Number.isFinite(value) ? value : "";
  if (typeof value === "boolean") return value ? "true" : "false";
  if (value === undefined || value === null) return "";
  return String(value);
}

function summarizeDiode(diode) {
  return {
    name: diode.name,
    anode: diode.anode,
    cathode: diode.cathode,
    model: diode.model,
    vdVolt: diode.vd,
    rawVdVolt: diode.rawVd,
    idAmp: diode.id,
    idMicroAmp: diode.id * 1e6,
    gdSiemens: diode.gd,
    gdMicroSiemens: diode.gd * 1e6,
    rdOhm: diode.rd,
    rdKiloOhm: diode.rd / 1e3,
    inStatedCurrentRange: diode.inStatedCurrentRange,
  };
}

function operatingPointSummary(result, netlist, vcmd) {
  const nodes = {};
  for (const [idx, node] of result.nodes.entries()) {
    nodes[node] = result.solution[idx];
  }
  const diodes = result.diodeInfo.map(summarizeDiode);
  const byName = Object.fromEntries(diodes.map((d) => [d.name, d]));
  const leftPathVolt = (byName.D3_SSD55?.vdVolt ?? 0) + (byName.D4_OA154Q?.vdVolt ?? 0);
  const rightPathVolt = (byName.D2_SSD55?.vdVolt ?? 0) + (byName.D1_OA154Q?.vdVolt ?? 0);

  return {
    vcmd,
    iterations: result.iterations,
    converged: result.converged,
    nodes,
    sourceCurrentAmp: result.solution[result.nodes.length] ?? null,
    diodeConductances: diodeConductanceMap(result.diodeInfo),
    diodes,
    allDiodesInStatedRange: diodes.every((d) => d.inStatedCurrentRange),
    anyDiodeOutOfStatedRange: diodes.some((d) => !d.inStatedCurrentRange),
    leftPathVolt,
    rightPathVolt,
    netlistTitle: netlist.title,
  };
}

function solveOperatingPoints(commands, options = {}) {
  const out = new Map();
  let previous = null;

  for (const vcmd of commands) {
    const dcNetlist = buildB6ParametricDcNetlist(vcmd, options);
    const dcResult = solveDc(dcNetlist, {
      initialGuess: previous,
      damping: 0.35,
      tolerance: 1e-10,
      maxIterations: 240,
    });
    if (!dcResult.converged) {
      throw new Error(`B6 empirical DC operating point did not converge for VCMD=${vcmd}`);
    }
    previous = dcResult.solution;
    out.set(vcmd, operatingPointSummary(dcResult, dcNetlist, vcmd));
  }

  return out;
}

function scenarioRows(scenario, operatingPoints, commands, frequencies) {
  const rows = [];

  for (const vcmd of commands) {
    const op = operatingPoints.get(vcmd);
    for (const frequency of frequencies) {
      const acNetlist = buildB6ParametricAcNetlist(frequency, op.diodeConductances, scenario.options);
      const ac = solveAc(acNetlist);
      const s = ac.solution;
      const vna = s.NA || c(0, 0);
      const vnb = s.NB || c(0, 0);
      const vcmdNode = s.CMD || c(0, 0);
      const vacCurrent = s["I(VAC)"] || c(0, 0);

      rows.push({
        scenario: scenario.name,
        note: scenario.note,
        vcmd,
        frequency,
        commandModel: acNetlist.commandModel,
        c4AbglFarad: acNetlist.parameters.c4Abgl,
        c6AbglFarad: acNetlist.parameters.c6Abgl,
        r7EffectiveOhm: acNetlist.parameters.r7,
        r8EffectiveOhm: acNetlist.parameters.r8,
        zcmdOhm: acNetlist.parameters.zcmd,
        allDiodesInStatedRange: op.allDiodesInStatedRange,
        anyDiodeOutOfStatedRange: op.anyDiodeOutOfStatedRange,
        leftPathVolt: op.leftPathVolt,
        rightPathVolt: op.rightPathVolt,
        VNA: serializeComplex(vna),
        VNB: serializeComplex(vnb),
        VCMD_NODE: serializeComplex(vcmdNode),
        IVAC: serializeComplex(vacCurrent),
        inputCurrentMagnitude: abs(vacCurrent),
        inputImpedanceMagnitude: abs(vacCurrent) > 0 ? 1 / abs(vacCurrent) : Infinity,
      });
    }
  }

  return rows;
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) {
    lines.push(columns.map((column) => csvCell(row[column])).join(","));
  }
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
}

function operatingPointRows(operatingPoints) {
  const rows = [];
  for (const op of operatingPoints.values()) {
    for (const d of op.diodes) {
      rows.push({
        vcmd: op.vcmd,
        iterations: op.iterations,
        diode: d.name,
        vd_v: d.vdVolt,
        raw_vd_v: d.rawVdVolt,
        id_ua: d.idMicroAmp,
        gd_us: d.gdMicroSiemens,
        rd_kohm: d.rdKiloOhm,
        in_stated_range: d.inStatedCurrentRange,
        left_path_v: op.leftPathVolt,
        right_path_v: op.rightPathVolt,
      });
    }
  }
  return rows;
}

function directTwoDiodeSeriesCheck(totalVolt = 1.0) {
  const perDiodeMilliVolt = (totalVolt / 2) * 1e3;
  const currentMicroAmp = diodeCurrentMicroAmpFromVoltageMilliVolt(perDiodeMilliVolt);
  const rdPerDiodeKiloOhm = dynamicResistanceKiloOhmFromCurrentMicroAmp(currentMicroAmp);
  return {
    totalVolt,
    perDiodeVolt: totalVolt / 2,
    currentMicroAmp,
    rdPerDiodeKiloOhm,
    rdSeriesKiloOhm: 2 * rdPerDiodeKiloOhm,
    note:
      "Direct check only: two identical empirical diode symbols in series with 1 V total. This is not the same as VCMD=1 V in the full B6 netlist because R10/R9 and bridge loading reduce diode path voltage.",
  };
}

function runSweep() {
  const commands = [0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0];
  const frequencies = [40, 100, 1000, 5000, 15000, 20000];
  const baseOptions = {
    bridgeDiodeLaw: "u273_empirical",
    r7EffectiveOhm: 100,
    r8EffectiveOhm: 250e3,
    rAmpInputOhm: Infinity,
  };
  const scenarios = [
    {
      name: "empirical_cmd_ideal_c4c6_0",
      note: "Command node held at AC ground; empirical U273 bridge diode law.",
      options: { ...baseOptions, commandAcMode: "ideal", zcmdOhm: 0, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
    {
      name: "empirical_cmd_10k_c4c6_0",
      note: "Finite B11 output impedance sensitivity; empirical U273 bridge diode law.",
      options: { ...baseOptions, zcmdOhm: 10e3, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
    {
      name: "empirical_cmd_100k_c4c6_0",
      note: "Finite B11 output impedance sensitivity; empirical U273 bridge diode law.",
      options: { ...baseOptions, zcmdOhm: 100e3, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
    {
      name: "empirical_cmd_open_c4c6_0",
      note: "Command port left open in AC; empirical U273 bridge diode law.",
      options: { ...baseOptions, commandAcMode: "open", zcmdOhm: Infinity, c4AbglFarad: 0, c6AbglFarad: 0 },
    },
  ];

  const operatingPoints = solveOperatingPoints(commands, baseOptions);
  const rows = scenarios.flatMap((scenario) => scenarioRows(scenario, operatingPoints, commands, frequencies));
  const opRows = operatingPointRows(operatingPoints);
  const vcmd1 = operatingPoints.get(1.0);
  const directPair1V = directTwoDiodeSeriesCheck(1.0);

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b6_empirical_ac_sweep.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 empirical diode AC sweep",
        status: "B6 bridge integrated with Siemens empirical diode law; VCMD is still externally imposed for this subcircuit sweep",
        warning:
          "This replaces placeholder Shockley diode parameters with the Siemens U_D/I_D/r_d law. It still does not close the full B11 feedback loop, transistor B6 stages, or final C4/C6/R7/R8 settings.",
        diodeLaw:
          "Applied to each B6 schematic diode D1-D4. The direct two-diode 1 V check is separate from the full B6 VCMD sweep because R10/R9 and bridge loading reduce diode path voltage.",
        directTwoDiodeSeriesCheck1V: directPair1V,
        commands,
        frequencies,
        scenarios,
        operatingPoints: Object.fromEntries(operatingPoints),
        siemensExampleCheckAtVcmd1: vcmd1
          ? {
              leftPathVolt: vcmd1.leftPathVolt,
              rightPathVolt: vcmd1.rightPathVolt,
              diodeCurrentsMicroAmp: Object.fromEntries(vcmd1.diodes.map((d) => [d.name, d.idMicroAmp])),
              expectedOrder:
                "Siemens example gives I1 ~= 18.33 uA for 1 V across the bridge diode calculation. In this extracted B6 subcircuit, VCMD=1 V produces lower diode path voltage through R10/R9 and passive loading.",
            }
          : null,
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b6_empirical_ac_sweep.csv");
  writeCsv(csvPath, rows.map((row) => ({
    scenario: row.scenario,
    vcmd: row.vcmd,
    frequency_hz: row.frequency,
    command_model: row.commandModel,
    zcmd_ohm: row.zcmdOhm,
    all_diodes_in_range: row.allDiodesInStatedRange,
    any_diode_out_of_range: row.anyDiodeOutOfStatedRange,
    left_path_v: row.leftPathVolt,
    right_path_v: row.rightPathVolt,
    VNA_mag: row.VNA.mag,
    VNA_db: db20(row.VNA.mag),
    VNB_mag: row.VNB.mag,
    VNB_db: db20(row.VNB.mag),
    VCMD_NODE_mag: row.VCMD_NODE.mag,
    input_impedance_mag: row.inputImpedanceMagnitude,
  })), [
    "scenario",
    "vcmd",
    "frequency_hz",
    "command_model",
    "zcmd_ohm",
    "all_diodes_in_range",
    "any_diode_out_of_range",
    "left_path_v",
    "right_path_v",
    "VNA_mag",
    "VNA_db",
    "VNB_mag",
    "VNB_db",
    "VCMD_NODE_mag",
    "input_impedance_mag",
  ]);

  const opCsvPath = path.join(outDir, "b6_empirical_operating_points.csv");
  writeCsv(opCsvPath, opRows, [
    "vcmd",
    "iterations",
    "diode",
    "vd_v",
    "raw_vd_v",
    "id_ua",
    "gd_us",
    "rd_kohm",
    "in_stated_range",
    "left_path_v",
    "right_path_v",
  ]);

  console.log(`Wrote ${jsonPath}`);
  console.log(`Wrote ${csvPath}`);
  console.log(`Wrote ${opCsvPath}`);
  console.log("");

  for (const scenario of scenarios) {
    const at1k = rows.find((row) => row.scenario === scenario.name && row.vcmd === 1 && row.frequency === 1000);
    const at15k = rows.find((row) => row.scenario === scenario.name && row.vcmd === 1 && row.frequency === 15000);
    console.log(
      `${scenario.name}: VCMD=1V |VNA|@1k=${at1k.VNA.mag.toExponential(6)} (${db20(at1k.VNA.mag).toFixed(2)} dB), ` +
      `|VNA|@15k=${at15k.VNA.mag.toExponential(6)} (${db20(at15k.VNA.mag).toFixed(2)} dB)`
    );
  }

  if (vcmd1) {
    const currents = Object.fromEntries(vcmd1.diodes.map((d) => [d.name, d.idMicroAmp.toFixed(3)]));
    console.log("");
    console.log(`VCMD=1V path volts: left=${vcmd1.leftPathVolt.toFixed(6)} V, right=${vcmd1.rightPathVolt.toFixed(6)} V`);
    console.log(`VCMD=1V diode currents uA: ${JSON.stringify(currents)}`);
    console.log(
      `Direct 2-diode 1V check: I=${directPair1V.currentMicroAmp.toFixed(3)} uA, ` +
      `series rd=${directPair1V.rdSeriesKiloOhm.toFixed(3)} kOhm`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  solveOperatingPoints,
  scenarioRows,
  runSweep,
};
