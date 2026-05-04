# U273 Component-Level State-Space Solver

## Boundary

This solver is an offline scientific reference engine. It is not the realtime
plugin engine yet, and it does not use WDF.

Current status:

```text
FULL_ACTIVE_MODEL_UNVERIFIED
```

The existing project-level boundary remains:

```text
PASS_WITH_GUARDED_BOUNDARIES
```

until the component-level solver is validated against the U273 golden DC, AC,
and transient datasets.

## Numerical Model

The circuit is represented as an implicit state-space / DAE system derived from
MNA:

```text
F(z, zdot, u, p) = 0
z = [x, a]
```

where `x` are dynamic energy states and `a` are algebraic MNA variables such as
node voltages and ideal-source branch currents.

The assembled charge/current form is:

```text
Q(z) * dz/dt + G(z, p) + i_nl(z, p) = B * u
```

The first implemented integration method is backward Euler:

```text
R(z_k) = Q(z_k) * (z_k - z_{k-1}) / h + G(z_k) + i_nl(z_k) - B * u_k
```

Each implicit step solves:

```text
J(z_n) * delta = -R(z_n)
z_(n+1) = z_n + alpha * delta
```

with damped Newton-Raphson and dense LU with partial pivoting. A trapezoidal
capacitor companion is also implemented, but backward Euler is the conservative
default for initial nonlinear closure.

## Component Scope

Implemented stamps:

- Resistor: linear MNA conductance stamp.
- Capacitor: backward Euler and trapezoidal companion stamps.
- Current source and voltage source.
- Diode: Shockley law with analytic conductance and exponential limiting.
- NPN BJT: simplified Ebers-Moll terminal-current model with analytic
  Jacobian.
- Ideal transformer boundary metadata.

Transformer policy for this sprint:

```text
U1/U2 are ideal fixed ports only.
No magnetic model, hysteresis, leakage, saturation, or custom transformer method
is included in this sprint.
```

## U273 Fixture

`U273ReferenceCircuitBuilder::buildB6BridgeSkeleton()` creates a first
component-level B6 bridge skeleton using the closed schematic values from the
previous analog steps:

- B6.R5 = 5.6 kOhm
- B6.C1 = 0.68 uF
- B6.C2 = 22 uF
- B6.R10 = 20 kOhm
- B6.D1/D4 OA154 approximation
- B6.D2/D3 SSD55 approximation
- U1 represented as ideal secondary-port metadata

This is a numerical skeleton, not final U273 equivalence.

`U273NetlistLoader::loadFromFile()` now loads the orchestrated JS netlist
`results/u273_netlist.json` into a C++ `CircuitGraph`. The loader stamps
resistors, capacitors, voltage/current sources, bridge diodes, and
potentiometers split at a controlled wiper fraction. Switches and transformer
magnetics remain explicit boundary metadata. BJT devices stay guarded by
default; known-terminal BJT hypotheses can be stamped only when
`BjtStampPolicy::stampKnownTerminals` is selected for calibration experiments.

The loader adds optional 1 Teraohm node gmin anchors to keep early full-netlist
DAE experiments numerically well posed without pretending those anchors are
physical schematic components.

## Realtime Bridge

`U273DspEngine` now calls `AnalogRealtimeEngine` instead of the old
`B6BridgeRealtimeModel` surrogate path. The realtime engine uses the empirical
B6 diode-bridge law captured from the project notes:

```text
V_mV = 308 * I_uA^0.16
rd_ohm = 48300 * I_uA^-0.84
```

This is a reduced realtime analog engine, not the final dense DAE engine. Its
boundary is therefore:

```text
FULL_ACTIVE_MODEL_UNVERIFIED
```

## Scientific References

- Falaize and Helie, "Passive Guaranteed Simulation of Analog Audio Circuits: A
  Port-Hamiltonian Approach", Applied Sciences, 2016.
  https://www.mdpi.com/2076-3417/6/10/273
- Medine, "Dynamical Systems for Audio Synthesis: Embracing Nonlinearities and
  Delay-Free Loops", Applied Sciences, 2016.
  https://www.mdpi.com/2076-3417/6/5/134
- Marz and Tischendorf, "Recent Results in Solving Index-2
  Differential-Algebraic Equations in Circuit Simulation", SIAM Journal on
  Scientific Computing.
  https://epubs.siam.org/doi/10.1137/S1064827595287250
- Ducceschi and Bilbao, "Non-iterative simulation methods for virtual analog
  modelling", IEEE/ACM TASLP, 2022.
  https://www.research.ed.ac.uk/en/publications/non-iterative-simulation-methods-for-virtual-analog-modelling
- Brambilla, Premoli, and Storti Gajani, "Recasting modified nodal analysis to
  improve reliability in numerical circuit simulation", IEEE TCAS-I, 2005.
  https://doi.org/10.1109/TCSI.2004.842869
