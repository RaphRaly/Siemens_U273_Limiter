"use strict";

const fs = require("node:fs");
const path = require("node:path");

const V = Object.freeze({
  V24: 24.0,
  N215: 21.5,
  N145: 14.5,
  N10: 10.0,
  N9: 9.0,
  N05: 0.5,
  REF: 0.0,
});

const R = Object.freeze({
  R10: 2e3,
  R11: 10e3,
  R12: 120,
  R13: 220e3,
  R14: 2e3,
  R15: 1.2e3,
  R16: 1.2e3,
  R17: 6.8e3,
  R9: 56e3,
  R7: 500,
  R8: 680,
  R7R8_MIN: 680,
  R7R8_MAX: 1180,
  R1: 3e3,
  R2_VISIBLE: 1e6,
});

const C = Object.freeze({
  C5: 100e-6,
  C6: 1e-9,
});

const betaSweep = Object.freeze([10, 20, 50, 100, 200]);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function current(fromVolt, toVolt, resistanceOhm) {
  return (fromVolt - toVolt) / resistanceOhm;
}

function n05Demand() {
  const iR12 = current(V.N05, V.REF, R.R12);
  const iR9 = current(V.N05, V.REF, R.R9);
  const iR7R8LowIfDcDirect = current(V.N05, V.REF, R.R7R8_MAX);
  const iR7R8HighIfDcDirect = current(V.N05, V.REF, R.R7R8_MIN);
  const iR7R8Dc = 0;
  const n05Dc = iR12 + iR9;
  return {
    iR12,
    iR9,
    iR7R8Dc,
    iR7R8LowIfDcDirect,
    iR7R8HighIfDcDirect,
    withoutR17: n05Dc,
    previousLowIfR7R8MistakenAsDc: n05Dc + iR7R8LowIfDcDirect,
    previousHighIfR7R8MistakenAsDc: n05Dc + iR7R8HighIfDcDirect,
  };
}

function passiveKcl() {
  const demand = n05Demand();
  const iR10 = current(V.V24, V.N215, R.R10);
  const iR11 = current(V.N215, V.N10, R.R11);
  const iR15 = current(V.V24, V.N145, R.R15);
  const iR13 = current(V.N9, V.N05, R.R13);
  const iR16 = current(V.N9, V.REF, R.R16);
  const n9PassiveDemand = iR16 + iR13;

  return {
    iR10,
    iR11,
    n215ResidualFromPrintedVoltagesAmp: iR10 - iR11,
    iR15,
    iR13,
    iR16,
    n9PassiveDemandAmp: n9PassiveDemand,
    n145MinusN9PassiveDemandAmp: iR15 - n9PassiveDemand,
    r14c6DcCurrentAmp: 0,
    c5DcCurrentAmp: 0,
    iR12: demand.iR12,
    iR9: demand.iR9,
    iR7R8Dc: demand.iR7R8Dc,
    n05DemandWithoutR17Amp: demand.withoutR17,
    n05PreviousDemandLowIfR7R8MistakenAsDcAmp: demand.previousLowIfR7R8MistakenAsDc,
    n05PreviousDemandHighIfR7R8MistakenAsDcAmp: demand.previousHighIfR7R8MistakenAsDc,
    n05DeficitAfterR13Amp: demand.withoutR17 - iR13,
  };
}

function r14c6AcRows() {
  return [20, 1e3, 20e3, 79_577.4715, 100e3, 1e6].map((frequencyHz) => {
    const w = 2 * Math.PI * frequencyHz;
    const x = w * R.R14 * C.C6;
    const magnitude = (w * C.C6) / Math.sqrt(1 + x * x);
    const phaseRad = Math.PI / 2 - Math.atan(x);
    return {
      frequencyHz,
      admittanceMagnitudeSiemens: magnitude,
      phaseDeg: phaseRad * 180 / Math.PI,
      conductanceR14Ratio: magnitude / (1 / R.R14),
      note: "AC-only branch N145--R14--C6--N10; DC stamp is open.",
    };
  });
}

function ts2IfR16LowerRefRows() {
  const p = passiveKcl();
  return betaSweep.map((beta) => {
    const collectorLikeN9Current = p.n9PassiveDemandAmp;
    const baseLikeCurrent = p.iR15 - collectorLikeN9Current;
    const effectiveBetaIfR16LowerRef = baseLikeCurrent > 0
      ? collectorLikeN9Current / baseLikeCurrent
      : Infinity;

    const collectorForThisBeta = p.iR15 * beta / (beta + 1);
    const requiredR16Current = collectorForThisBeta - p.iR13;
    const requiredR16LowerVolt = V.N9 - requiredR16Current * R.R16;

    return {
      beta,
      r15CurrentAmp: p.iR15,
      n9PassiveDemandAmp: p.n9PassiveDemandAmp,
      residualFromR15AfterN9PassiveAmp: baseLikeCurrent,
      effectiveCurrentRatio: effectiveBetaIfR16LowerRef,
      collectorCurrentForBetaAmp: collectorForThisBeta,
      requiredR16CurrentForBetaAmp: requiredR16Current,
      requiredR16LowerVoltForBeta: requiredR16LowerVolt,
      verdict: Math.abs(requiredR16LowerVolt) < 0.2
        ? "coherent_if_R16_lower_is_reference"
        : "requires_pin_or_beta_review",
      note: "R16 lower endpoint is now confirmed as reference. This row is still only a current-sign check, not a B/C/E assignment.",
    };
  });
}

function topologyRows() {
  return [
    {
      item: "R10",
      visibleConnection: "V24_to_N215",
      dcStamp: "g10 between V24 and N215",
      status: "confirmed_by_user_crop",
    },
    {
      item: "R11",
      visibleConnection: "N215_to_N10",
      dcStamp: "g11 between N215 and N10",
      status: "confirmed_by_user_crop",
    },
    {
      item: "R15",
      visibleConnection: "V24_to_N145",
      dcStamp: "g15 between V24 and N145",
      status: "corrects_latest_path_file_that_used_N215_to_N145",
    },
    {
      item: "R14_C6",
      visibleConnection: "N145_to_R14_to_X_R14C6_to_C6_to_N10",
      dcStamp: "open",
      status: "corrects_previous_N05_endpoint_assumption",
    },
    {
      item: "R13",
      visibleConnection: "N9_to_N05",
      dcStamp: "g13 between N9 and N05",
      status: "confirmed_by_user_crop",
    },
    {
      item: "R16",
      visibleConnection: "N9_to_reference",
      dcStamp: "g16 between N9 and REF",
      status: "confirmed_by_second_user_crop",
    },
    {
      item: "R12_R9",
      visibleConnection: "N05_to_reference",
      dcStamp: "g12 and g9 between N05 and REF",
      status: "confirmed_by_second_user_crop",
    },
    {
      item: "R7_R8",
      visibleConnection: "N05_to_C5_100uF_to_N150; N150_to_R7_500_adjustable_to_R8_680_to_reference",
      dcStamp: "open from N05 because C5 is series/open in DC",
      status: "corrected_by_global_user_crop",
    },
    {
      item: "R17_C10",
      visibleConnection: "N4_to_R17_6k8_parallel_C10_100p_to_reference",
      dcStamp: "g17 between N4 and REF; C10 open in DC and dynamic in AC/transient",
      status: "confirmed_by_screenshot_092120_not_N05",
    },
    {
      item: "S6_core",
      visibleConnection: "R5/C3 between left and right nodes; R4/C2 and R3/C1 split through VM; C4/R6 from VM to reference",
      dcStamp: "S6 contact table still guarded",
      status: "visible_in_global_user_crop",
    },
    {
      item: "R1_R2",
      visibleConnection: "right side of S6 shows R1=3k and R2 visible as 1M adjustable",
      dcStamp: "guarded until exact S6 position/contact routing is assigned",
      status: "corrects_previous_R2_5k1_read_for_this_visible_branch",
    },
    {
      item: "Ts1_Ts2",
      visibleConnection: "SST117 terminals are visible around N145/N10/N9 and N10/N05",
      dcStamp: "guarded",
      status: "pin names B/C/E still unassigned",
    },
  ];
}

function writeCsv(filePath, rows, columns) {
  const lines = [columns.join(",")];
  for (const row of rows) {
    lines.push(columns.map((column) => row[column]).join(","));
  }
  fs.writeFileSync(filePath, `${lines.join("\n")}\n`);
}

function run() {
  const outDir = path.join(__dirname, "..", "results");
  ensureDir(outDir);

  const passive = passiveKcl();
  const acRows = r14c6AcRows();
  const ts2Rows = ts2IfR16LowerRefRows();
  const topo = topologyRows();

  const jsonPath = path.join(outDir, "b11_crop_ts1_ts2_corrected_topology.json");
  fs.writeFileSync(
    jsonPath,
    JSON.stringify(
      {
        title: "Siemens U273 B11 Ts1/Ts2 topology corrected from user crop",
        status: "topology correction; BJT terminal stamps still guarded",
        source: "user supplied Ts1/Ts2 crops in chat, 2026-05-03",
        visibleDcNodes: V,
        resistorValues: R,
        capacitorValues: C,
        topologyRows: topo,
        passiveKcl: passive,
        r14c6AcRows: acRows,
        ts2IfR16LowerRefRows: ts2Rows,
      },
      null,
      2
    )
  );

  writeCsv(
    path.join(outDir, "b11_crop_ts1_ts2_topology_rows.csv"),
    topo,
    ["item", "visibleConnection", "dcStamp", "status"]
  );
  writeCsv(
    path.join(outDir, "b11_crop_ts1_ts2_passive_kcl.csv"),
    [passive],
    [
      "iR10",
      "iR11",
      "n215ResidualFromPrintedVoltagesAmp",
      "iR15",
      "iR13",
      "iR16",
      "n9PassiveDemandAmp",
      "n145MinusN9PassiveDemandAmp",
      "r14c6DcCurrentAmp",
      "c5DcCurrentAmp",
      "iR12",
      "iR9",
      "iR7R8Dc",
      "n05DemandWithoutR17Amp",
      "n05PreviousDemandLowIfR7R8MistakenAsDcAmp",
      "n05PreviousDemandHighIfR7R8MistakenAsDcAmp",
      "n05DeficitAfterR13Amp",
    ]
  );
  writeCsv(
    path.join(outDir, "b11_crop_ts2_r16_lower_ref_check.csv"),
    ts2Rows,
    [
      "beta",
      "r15CurrentAmp",
      "n9PassiveDemandAmp",
      "residualFromR15AfterN9PassiveAmp",
      "effectiveCurrentRatio",
      "collectorCurrentForBetaAmp",
      "requiredR16CurrentForBetaAmp",
      "requiredR16LowerVoltForBeta",
      "verdict",
      "note",
    ]
  );
  writeCsv(
    path.join(outDir, "b11_crop_r14_c6_ac_rows.csv"),
    acRows,
    ["frequencyHz", "admittanceMagnitudeSiemens", "phaseDeg", "conductanceR14Ratio", "note"]
  );

  console.log(`Wrote ${jsonPath}`);
  console.log(`R15 V24->N145: ${(passive.iR15 * 1e3).toFixed(3)} mA`);
  console.log(`R16 N9->REF: ${(passive.iR16 * 1e3).toFixed(3)} mA`);
  console.log(`N9 passive demand R16+R13: ${(passive.n9PassiveDemandAmp * 1e3).toFixed(3)} mA`);
  console.log(`R15 minus N9 passive demand: ${(passive.n145MinusN9PassiveDemandAmp * 1e3).toFixed(3)} mA`);
  console.log(`R14/C6 DC current: ${(passive.r14c6DcCurrentAmp * 1e3).toFixed(3)} mA`);
  console.log(`R13 N9->N05: ${(passive.iR13 * 1e6).toFixed(3)} uA`);
  console.log(`C5/R7/R8 DC current from N05: ${(passive.c5DcCurrentAmp * 1e3).toFixed(3)} mA`);
  console.log(`N05 demand without R17: ${(passive.n05DemandWithoutR17Amp * 1e3).toFixed(3)} mA`);
  console.log(`N05 deficit after R13: ${(passive.n05DeficitAfterR13Amp * 1e3).toFixed(3)} mA`);
}

if (require.main === module) {
  run();
}

module.exports = {
  V,
  R,
  C,
  passiveKcl,
  r14c6AcRows,
  ts2IfR16LowerRefRows,
  topologyRows,
  run,
};
