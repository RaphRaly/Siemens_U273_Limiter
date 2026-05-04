"use strict";

// Shared DC modified-nodal-analysis core for research scripts. It keeps device
// linearization, matrix stamping, solving, and residual checks in one reusable
// module instead of duplicating them in every sweep.
const {
  diodeCurrentMicroAmpFromVoltageMilliVolt,
  dynamicResistanceKiloOhmFromCurrentMicroAmp,
} = require("./u273_diode_empirical");

const GROUND = "0";

function nodeVoltage(solution, node, nodeIndex) {
  if (!node || node === GROUND || node.toUpperCase() === "GND") return 0;
  const idx = nodeIndex.get(node);
  if (idx === undefined) throw new Error(`Unknown node: ${node}`);
  return solution[idx] || 0;
}

function stampConductance(A, n1, n2, g, nodeIndex) {
  const i = nodeIndex.get(n1);
  const j = nodeIndex.get(n2);

  if (i !== undefined) A[i][i] += g;
  if (j !== undefined) A[j][j] += g;
  if (i !== undefined && j !== undefined) {
    A[i][j] -= g;
    A[j][i] -= g;
  }
}

function stampCurrentInjection(rhs, nPlus, nMinus, current, nodeIndex) {
  // Positive current flows from nPlus to nMinus, so it leaves nPlus and enters nMinus.
  const i = nodeIndex.get(nPlus);
  const j = nodeIndex.get(nMinus);
  if (i !== undefined) rhs[i] -= current;
  if (j !== undefined) rhs[j] += current;
}

function diodeLinearization(vd, params) {
  if (params.model === "u273_empirical") {
    const reverseGmin = params.reverseGmin ?? 1e-12;
    const maxVd = params.maxVd ?? 1.2;
    const rawVd = vd;

    if (vd <= 0) {
      return {
        id: reverseGmin * vd,
        gd: reverseGmin,
        ieq: 0,
        vd,
        rawVd,
        model: "u273_empirical",
        currentMicroAmp: reverseGmin * vd * 1e6,
        dynamicResistanceOhm: 1 / reverseGmin,
        inStatedCurrentRange: false,
      };
    }

    const v = Math.min(maxVd, vd);
    const voltageMilliVolt = v * 1e3;
    const currentMicroAmp = diodeCurrentMicroAmpFromVoltageMilliVolt(voltageMilliVolt);
    const dynamicResistanceOhm =
      dynamicResistanceKiloOhmFromCurrentMicroAmp(currentMicroAmp) * 1e3;
    const id = currentMicroAmp * 1e-6;
    const gd = 1 / dynamicResistanceOhm;
    const ieq = id - gd * v;

    return {
      id,
      gd,
      ieq,
      vd: v,
      rawVd,
      model: "u273_empirical",
      voltageMilliVolt,
      currentMicroAmp,
      dynamicResistanceOhm,
      inStatedCurrentRange: currentMicroAmp >= 2 && currentMicroAmp <= 500,
    };
  }

  const vt = params.vt ?? 25.852e-3;
  const n = params.n ?? 1.0;
  const is = params.is ?? 1e-15;
  const maxVd = params.maxVd ?? 0.85;
  const minVd = params.minVd ?? -1.0;
  const v = Math.max(minVd, Math.min(maxVd, vd));
  const ev = Math.exp(v / (n * vt));
  const id = is * (ev - 1);
  const gd = (is / (n * vt)) * ev;
  const ieq = id - gd * v;
  return { id, gd, ieq, vd: v };
}

function gaussianSolve(matrix, rhs) {
  const n = rhs.length;
  const A = matrix.map((row, i) => row.concat(rhs[i]));

  for (let col = 0; col < n; col++) {
    let pivot = col;
    let best = Math.abs(A[col][col]);
    for (let row = col + 1; row < n; row++) {
      const value = Math.abs(A[row][col]);
      if (value > best) {
        best = value;
        pivot = row;
      }
    }

    if (best < 1e-24) {
      throw new Error(`Singular matrix near column ${col}`);
    }

    if (pivot !== col) {
      const tmp = A[col];
      A[col] = A[pivot];
      A[pivot] = tmp;
    }

    const pivotValue = A[col][col];
    for (let c = col; c <= n; c++) A[col][c] /= pivotValue;

    for (let row = 0; row < n; row++) {
      if (row === col) continue;
      const factor = A[row][col];
      if (factor === 0) continue;
      for (let c = col; c <= n; c++) {
        A[row][c] -= factor * A[col][c];
      }
    }
  }

  return A.map((row) => row[n]);
}

function residualStats(system, solution) {
  let maxAbs = 0;
  let sumSquares = 0;
  const residual = system.A.map((row, rowIdx) => {
    let ax = 0;
    for (let col = 0; col < row.length; col++) ax += row[col] * solution[col];
    const value = ax - system.rhs[rowIdx];
    const absValue = Math.abs(value);
    if (absValue > maxAbs) maxAbs = absValue;
    sumSquares += value * value;
    return value;
  });

  return {
    residual,
    maxAbs,
    rms: residual.length ? Math.sqrt(sumSquares / residual.length) : 0,
  };
}

function collectNodes(netlist) {
  const nodes = new Set();
  function add(node) {
    if (!node || node === GROUND || node.toUpperCase() === "GND") return;
    nodes.add(node);
  }

  for (const r of netlist.resistors || []) {
    add(r.n1);
    add(r.n2);
  }
  for (const d of netlist.diodes || []) {
    add(d.anode);
    add(d.cathode);
  }
  for (const v of netlist.voltageSources || []) {
    add(v.nPlus);
    add(v.nMinus);
  }

  return Array.from(nodes).sort();
}

function buildSystem(netlist, guess) {
  const nodes = collectNodes(netlist);
  const nodeIndex = new Map(nodes.map((node, idx) => [node, idx]));
  const voltageSources = netlist.voltageSources || [];
  const voltageSourceOffset = nodes.length;
  const size = nodes.length + voltageSources.length;
  const A = Array.from({ length: size }, () => Array(size).fill(0));
  const rhs = Array(size).fill(0);
  const diodeInfo = [];

  for (const r of netlist.resistors || []) {
    stampConductance(A, r.n1, r.n2, 1 / r.value, nodeIndex);
  }

  for (const d of netlist.diodes || []) {
    const va = nodeVoltage(guess, d.anode, nodeIndex);
    const vk = nodeVoltage(guess, d.cathode, nodeIndex);
    const lin = diodeLinearization(va - vk, d);
    stampConductance(A, d.anode, d.cathode, lin.gd, nodeIndex);
    stampCurrentInjection(rhs, d.anode, d.cathode, lin.ieq, nodeIndex);
    diodeInfo.push({
      name: d.name,
      anode: d.anode,
      cathode: d.cathode,
      vd: lin.vd,
      rawVd: lin.rawVd ?? lin.vd,
      id: lin.id,
      gd: lin.gd,
      rd: lin.gd > 0 ? 1 / lin.gd : Infinity,
      model: lin.model ?? "shockley",
      currentMicroAmp: lin.currentMicroAmp,
      voltageMilliVolt: lin.voltageMilliVolt,
      dynamicResistanceOhm: lin.dynamicResistanceOhm,
      inStatedCurrentRange: lin.inStatedCurrentRange,
    });
  }

  voltageSources.forEach((v, sIdx) => {
    const row = voltageSourceOffset + sIdx;
    const currentIdx = row;
    const p = nodeIndex.get(v.nPlus);
    const m = nodeIndex.get(v.nMinus);

    if (p !== undefined) {
      A[p][currentIdx] += 1;
      A[row][p] += 1;
    }
    if (m !== undefined) {
      A[m][currentIdx] -= 1;
      A[row][m] -= 1;
    }
    rhs[row] += v.value;
  });

  return { A, rhs, nodes, nodeIndex, diodeInfo };
}

function solveDc(netlist, options = {}) {
  const tolerance = options.tolerance ?? 1e-9;
  const residualTolerance = options.residualTolerance ?? 1e-9;
  const maxIterations = options.maxIterations ?? 80;
  const damping = options.damping ?? 1.0;
  const nodes = collectNodes(netlist);
  const size = nodes.length + (netlist.voltageSources || []).length;
  let x = options.initialGuess ? options.initialGuess.slice() : Array(size).fill(0);
  let finalSystem = null;
  let finalResidual = null;

  for (let iter = 0; iter < maxIterations; iter++) {
    const system = buildSystem(netlist, x);
    const xNewRaw = gaussianSolve(system.A, system.rhs);
    const xNew = x.map((value, idx) => value + damping * (xNewRaw[idx] - value));
    const delta = Math.max(...xNew.map((value, idx) => Math.abs(value - x[idx])));
    x = xNew;
    finalSystem = system;

    if (delta < tolerance) {
      const checkSystem = buildSystem(netlist, x);
      finalResidual = residualStats(checkSystem, x);
      if (finalResidual.maxAbs > residualTolerance) continue;
      return {
        converged: true,
        iterations: iter + 1,
        solution: x,
        nodes: checkSystem.nodes,
        diodeInfo: checkSystem.diodeInfo,
        residual: finalResidual,
      };
    }
  }

  if (finalSystem) finalResidual = residualStats(buildSystem(netlist, x), x);

  return {
    converged: false,
    iterations: maxIterations,
    solution: x,
    nodes,
    diodeInfo: finalSystem ? finalSystem.diodeInfo : [],
    residual: finalResidual,
  };
}

function solutionObject(result, netlist) {
  const out = {};
  result.nodes.forEach((node, idx) => {
    out[node] = result.solution[idx];
  });
  const offset = result.nodes.length;
  (netlist.voltageSources || []).forEach((src, idx) => {
    out[`I(${src.name})`] = result.solution[offset + idx];
  });
  return out;
}

module.exports = {
  solveDc,
  solutionObject,
  residualStats,
};
