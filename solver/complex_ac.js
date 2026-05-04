"use strict";

// Small complex-number AC solver for exploratory sweeps. It is intentionally
// dependency-free so the historical calculations remain easy to rerun.
function c(re, im = 0) {
  return { re, im };
}

function add(a, b) {
  return c(a.re + b.re, a.im + b.im);
}

function sub(a, b) {
  return c(a.re - b.re, a.im - b.im);
}

function mul(a, b) {
  return c(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

function div(a, b) {
  const d = b.re * b.re + b.im * b.im;
  return c((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}

function neg(a) {
  return c(-a.re, -a.im);
}

function abs(a) {
  return Math.hypot(a.re, a.im);
}

function stampAdmittance(A, n1, n2, y, nodeIndex) {
  const i = nodeIndex.get(n1);
  const j = nodeIndex.get(n2);
  if (i !== undefined) A[i][i] = add(A[i][i], y);
  if (j !== undefined) A[j][j] = add(A[j][j], y);
  if (i !== undefined && j !== undefined) {
    A[i][j] = sub(A[i][j], y);
    A[j][i] = sub(A[j][i], y);
  }
}

function stampVccs(A, nPlus, nMinus, ctrlPlus, ctrlMinus, y, nodeIndex) {
  // Positive current flows from nPlus to nMinus and is controlled by
  // y * (V(ctrlPlus) - V(ctrlMinus)).
  const p = nodeIndex.get(nPlus);
  const m = nodeIndex.get(nMinus);
  const cp = nodeIndex.get(ctrlPlus);
  const cm = nodeIndex.get(ctrlMinus);

  if (p !== undefined && cp !== undefined) A[p][cp] = add(A[p][cp], y);
  if (p !== undefined && cm !== undefined) A[p][cm] = sub(A[p][cm], y);
  if (m !== undefined && cp !== undefined) A[m][cp] = sub(A[m][cp], y);
  if (m !== undefined && cm !== undefined) A[m][cm] = add(A[m][cm], y);
}

function collectNodes(netlist) {
  const nodes = new Set();
  const addNode = (n) => {
    if (!n || n === "0" || n.toUpperCase() === "GND") return;
    nodes.add(n);
  };
  for (const x of netlist.admittances || []) {
    addNode(x.n1);
    addNode(x.n2);
  }
  for (const x of netlist.vccs || []) {
    addNode(x.nPlus);
    addNode(x.nMinus);
    addNode(x.ctrlPlus);
    addNode(x.ctrlMinus);
  }
  for (const x of netlist.voltageSources || []) {
    addNode(x.nPlus);
    addNode(x.nMinus);
  }
  return Array.from(nodes).sort();
}

function gaussianSolveComplex(matrix, rhs) {
  const n = rhs.length;
  const A = matrix.map((row, i) => row.map((x) => c(x.re, x.im)).concat(c(rhs[i].re, rhs[i].im)));

  for (let col = 0; col < n; col++) {
    let pivot = col;
    let best = abs(A[col][col]);
    for (let row = col + 1; row < n; row++) {
      const value = abs(A[row][col]);
      if (value > best) {
        best = value;
        pivot = row;
      }
    }
    if (best < 1e-24) throw new Error(`Singular complex matrix near column ${col}`);

    if (pivot !== col) {
      const tmp = A[col];
      A[col] = A[pivot];
      A[pivot] = tmp;
    }

    const pivotValue = A[col][col];
    for (let k = col; k <= n; k++) A[col][k] = div(A[col][k], pivotValue);

    for (let row = 0; row < n; row++) {
      if (row === col) continue;
      const factor = A[row][col];
      if (abs(factor) === 0) continue;
      for (let k = col; k <= n; k++) {
        A[row][k] = sub(A[row][k], mul(factor, A[col][k]));
      }
    }
  }

  return A.map((row) => row[n]);
}

function solveAc(netlist) {
  const nodes = collectNodes(netlist);
  const nodeIndex = new Map(nodes.map((node, idx) => [node, idx]));
  const sources = netlist.voltageSources || [];
  const offset = nodes.length;
  const size = nodes.length + sources.length;
  const A = Array.from({ length: size }, () => Array.from({ length: size }, () => c(0, 0)));
  const rhs = Array.from({ length: size }, () => c(0, 0));

  for (const adm of netlist.admittances || []) {
    stampAdmittance(A, adm.n1, adm.n2, adm.y, nodeIndex);
  }

  for (const src of netlist.vccs || []) {
    stampVccs(A, src.nPlus, src.nMinus, src.ctrlPlus, src.ctrlMinus, src.y, nodeIndex);
  }

  sources.forEach((src, idx) => {
    const row = offset + idx;
    const currentIdx = row;
    const p = nodeIndex.get(src.nPlus);
    const m = nodeIndex.get(src.nMinus);
    if (p !== undefined) {
      A[p][currentIdx] = add(A[p][currentIdx], c(1));
      A[row][p] = add(A[row][p], c(1));
    }
    if (m !== undefined) {
      A[m][currentIdx] = sub(A[m][currentIdx], c(1));
      A[row][m] = sub(A[row][m], c(1));
    }
    rhs[row] = add(rhs[row], src.value);
  });

  const solution = gaussianSolveComplex(A, rhs);
  const out = {};
  nodes.forEach((node, idx) => {
    out[node] = solution[idx];
  });
  sources.forEach((src, idx) => {
    out[`I(${src.name})`] = solution[offset + idx];
  });
  return { nodes, solution: out };
}

module.exports = {
  c,
  abs,
  solveAc,
};
