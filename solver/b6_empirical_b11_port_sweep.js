"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { solveDc } = require("./mna_core");
const { c, abs, solveAc } = require("./complex_ac");
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
  return String(value).replaceAll(",", ";");
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) {
    lines.push(columns.map((column) => csvCell(row[column])).join(","));
  }
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
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

function solutionNodes(result) {
  const nodes = {};
  for (const [idx, node] of result.nodes.entries()) {
    nodes[node] = result.solution[idx];
  }
  return nodes;
}

function operatingPointSummary(result, netlist, scenario, driveVolt) {
  const nodes = solutionNodes(result);
  const diodes = result.diodeInfo.map(summarizeDiode);
  const byName = Object.fromEntries(diodes.map((d) => [d.name, d]));
  const leftPathVolt = (byName.D3_SSD55?.vdVolt ?? 0) + (byName.D4_OA154Q?.vdVolt ?? 0);
  const rightPathVolt = (byName.D2_SSD55?.vdVolt ?? 0) + (byName.D1_OA154Q?.vdVolt ?? 0);
  const sourceOhm = scenario.options.commandSourceOhm;
  const cmdVolt = nodes.CMD ?? 0;
  const b11DrvVolt = nodes.B11_DRV ?? driveVolt;
  const sourceCurrentAmp = (b11DrvVolt - cmdVolt) / sourceOhm;

  return {
    scenario: scenario.name,
    scenarioNote: scenario.note,
    driveVolt,
    commandDcModel: netlist.commandDcModel,
    sourceOhm,
    iterations: result.iterations,
    converged: result.converged,
    nodes,
    b11DrvVolt,
    cmdVolt,
    commandSagVolt: b11DrvVolt - cmdVolt,
    sourceCurrentAmp,
    sourceCurrentMicroAmp: sourceCurrentAmp * 1e6,
    diodeConductances: diodeConductanceMap(result.diodeInfo),
    diodes,
    allDiodesInStatedRange: diodes.every((d) => d.inStatedCurrentRange),
    anyDiodeOutOfStatedRange: diodes.some((d) => !d.inStatedCurrentRange),
    leftPathVolt,
    rightPathVolt,
  };
}

function solveScenarioOperatingPoints(scenario, driveVoltages) {
  const out = new Map();
  let previous = null;

  for (const driveVolt of driveVoltages) {
    const dcNetlist = buildB6ParametricDcNetlist(driveVolt, {
      ...scenario.options,
      commandDcMode: "thevenin",
      commandSourceVolt: driveVolt,
    });
    const dcResult = solveDc(dcNetlist, {
      initialGuess: previous,
      damping: 0.35,
      tolerance: 1e-10,
      maxIterations: 260,
    });
    if (!dcResult.converged) {
      throw new Error(`${scenario.name}: B6+B11-port DC operating point did not converge for VDRV=${driveVolt}`);
    }
    previous = dcResult.solution;
    out.set(driveVolt, operatingPointSummary(dcResult, dcNetlist, scenario, driveVolt));
  }

  return out;
}

function makeAcRows(scenario, operatingPoints, driveVoltages, frequencies) {
  const rows = [];

  for (const driveVolt of driveVoltages) {
    const op = operatingPoints.get(driveVolt);
    for (const frequency of frequencies) {
      const acNetlist = buildB6ParametricAcNetlist(frequency, op.diodeConductances, {
        ...scenario.options,
        commandAcMode: "finite",
        zcmdOhm: scenario.options.commandSourceOhm,
      });
      acNetlist.commandModel =
        `B11/S6/S7 finite small-signal output impedance ${scenario.options.commandSourceOhm} Ohm`;
      const ac = solveAc(acNetlist);
      const sol = ac.solution;
      const vna = sol.NA || c(0, 0);
      const vnb = sol.NB || c(0, 0);
      const vcmdNode = sol.CMD || c(0, 0);
      const vacCurrent = sol["I(VAC)"] || c(0, 0);

      rows.push({
        scenario: scenario.name,
        note: scenario.note,
        driveVolt,
        cmdDcVolt: op.cmdVolt,
        commandSagVolt: op.commandSagVolt,
        sourceOhm: op.sourceOhm,
        sourceCurrentMicroAmp: op.sourceCurrentMicroAmp,
        frequency,
        commandModel: acNetlist.commandModel,
        allDiodesInStatedRange: op.allDiodesInStatedRange,
        anyDiodeOutOfStatedRange: op.anyDiodeOutOfStatedRange,
        leftPathVolt: op.leftPathVolt,
        rightPathVolt: op.rightPathVolt,
        VNA: serializeComplex(vna),
        VNB: serializeComplex(vnb),
        VCMD_NODE: serializeComplex(vcmdNode),
        IVAC: serializeComplex(vacCurrent),
        inputImpedanceMagnitude: abs(vacCurrent) > 0 ? 1 / abs(vacCurrent) : Infinity,
      });
    }
  }

  return rows;
}

function operatingPointRows(allOperatingPoints) {
  const rows = [];
  for (const operatingPoints of allOperatingPoints.values()) {
    for (const op of operatingPoints.values()) {
      for (const diode of op.diodes) {
        rows.push({
          scenario: op.scenario,
          drive_v: op.driveVolt,
          cmd_v: op.cmdVolt,
          sag_v: op.commandSagVolt,
          source_ohm: op.sourceOhm,
          source_current_ua: op.sourceCurrentMicroAmp,
          iterations: op.iterations,
          diode: diode.name,
          vd_v: diode.vdVolt,
          raw_vd_v: diode.rawVdVolt,
          id_ua: diode.idMicroAmp,
          gd_us: diode.gdMicroSiemens,
          rd_kohm: diode.rdKiloOhm,
          in_stated_range: diode.inStatedCurrentRange,
          left_path_v: op.leftPathVolt,
          right_path_v: op.rightPathVolt,
        });
      }
    }
  }
  return rows;
}

function flattenOperatingPoints(allOperatingPoints) {
  const out = {};
  for (const [scenario, operatingPoints] of allOperatingPoints.entries()) {
    out[scenario] = Object.fromEntries(operatingPoints);
  }
  return out;
}

function runSweep() {
  const driveVoltages = [0, 0.5, 1, 2, 3, 5, 8, 12];
  const frequencies = [40, 100, 1000, 5000, 15000, 20000];
  const baseOptions = {
    bridgeDiodeLaw: "u273_empirical",
    r7EffectiveOhm: 100,
    r8EffectiveOhm: 250e3,
    rAmpInputOhm: Infinity,
    c4AbglFarad: 0,
    c6AbglFarad: 0,
  };
  const scenarios = [
    {
      name: "b11_s6_s7_delivery_linear_r3k",
      note:
        "Finite B11/S6/S7 port, delivery-linear S6 contact truth retained as operating context. Rsource=3k is a low-output-impedance bound tied to R1=3k in the visible S6/S7 area; active B11 remains reduced to Thevenin.",
      options: { ...baseOptions, commandSourceOhm: 3e3 },
    },
    {
      name: "b11_s6_s7_delivery_linear_r10k",
      note:
        "Finite B11/S6/S7 port with moderate output impedance. This is a sensitivity case until the active B11 regulator output resistance is closed.",
      options: { ...baseOptions, commandSourceOhm: 10e3 },
    },
    {
      name: "b11_s7_detector_r31_51k",
      note:
        "High-impedance detector-side bound using R31=51k as a physically visible series resistance near D2/ZL10/S7.",
      options: { ...baseOptions, commandSourceOhm: 51e3 },
    },
    {
      name: "b11_s6_s7_high_z_100k",
      note:
        "Upper sensitivity case. It is intentionally not claimed as the real B11 output impedance.",
      options: { ...baseOptions, commandSourceOhm: 100e3 },
    },
  ];

  const allOperatingPoints = new Map();
  for (const scenario of scenarios) {
    allOperatingPoints.set(scenario.name, solveScenarioOperatingPoints(scenario, driveVoltages));
  }

  const rows = scenarios.flatMap((scenario) =>
    makeAcRows(scenario, allOperatingPoints.get(scenario.name), driveVoltages, frequencies)
  );
  const opRows = operatingPointRows(allOperatingPoints);

  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const jsonPath = path.join(outDir, "b6_empirical_b11_port_sweep.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B6 empirical bridge driven by finite B11/S6/S7 command port",
        status:
          "VCMD ideal has been replaced by a B11/S6/S7 Thevenin command source for DC and by the same finite source impedance for AC.",
        scientificBoundary:
          "This is not yet the full active B11/S6/S7 closed loop. The active regulator, transistor output resistance, and exact detector discharge path remain parametric; the model no longer shorts CMD to an ideal voltage source.",
        s6S7ContactTruthUsed: {
          s6DeliveryLinear:
            "4/5 closed, 8/9 closed, 5/6 open, 7/8 open; 5->3->1 closed; 2 open.",
          s7DrawnLimiter:
            "17->18 closed, 19 open; opposite state 17->19 is retained as compressor hypothesis.",
        },
        driveVoltages,
        frequencies,
        scenarios,
        operatingPoints: flattenOperatingPoints(allOperatingPoints),
        rows,
      },
      null,
      2
    )
  );

  const csvPath = path.join(outDir, "b6_empirical_b11_port_sweep.csv");
  writeCsv(csvPath, rows.map((row) => ({
    scenario: row.scenario,
    drive_v: row.driveVolt,
    cmd_dc_v: row.cmdDcVolt,
    sag_v: row.commandSagVolt,
    source_ohm: row.sourceOhm,
    source_current_ua: row.sourceCurrentMicroAmp,
    frequency_hz: row.frequency,
    command_model: row.commandModel,
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
    "drive_v",
    "cmd_dc_v",
    "sag_v",
    "source_ohm",
    "source_current_ua",
    "frequency_hz",
    "command_model",
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

  const opCsvPath = path.join(outDir, "b6_empirical_b11_port_operating_points.csv");
  writeCsv(opCsvPath, opRows, [
    "scenario",
    "drive_v",
    "cmd_v",
    "sag_v",
    "source_ohm",
    "source_current_ua",
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
    const at1 = allOperatingPoints.get(scenario.name).get(1);
    const at3 = allOperatingPoints.get(scenario.name).get(3);
    const ac1k = rows.find((row) => row.scenario === scenario.name && row.driveVolt === 3 && row.frequency === 1000);
    console.log(
      `${scenario.name}: VDRV=1 -> CMD=${at1.cmdVolt.toFixed(6)} V, ` +
      `Icmd=${at1.sourceCurrentMicroAmp.toFixed(3)} uA; ` +
      `VDRV=3 -> CMD=${at3.cmdVolt.toFixed(6)} V, ` +
      `Icmd=${at3.sourceCurrentMicroAmp.toFixed(3)} uA, ` +
      `|VNA|@1k=${ac1k.VNA.mag.toExponential(6)} (${db20(ac1k.VNA.mag).toFixed(2)} dB)`
    );
  }
}

if (require.main === module) {
  runSweep();
}

module.exports = {
  solveScenarioOperatingPoints,
  makeAcRows,
  runSweep,
};
