# B6/B11 Active Closure Plan

## Decision

The Perplexity research result does not justify promoting the plugin runtime to
a validated full-active model. It confirms the existing project boundary:

```text
PASS_WITH_GUARDED_BOUNDARIES
```

for the shipped/realtime path, and:

```text
FULL_ACTIVE_MODEL_UNVERIFIED
```

for the offline component-level reference.

The next useful work is therefore offline reference closure: prove the B6/B11
topology, stamp the active devices in the loaded netlist, converge a physical DC
operating point, then validate AC, transient, and audio gates before deriving
any realtime surrogate.

## Current Project State

The repo already has useful infrastructure:

- MNA-style stamps for resistors, capacitors, voltage/current sources, diodes,
  and simplified NPN Ebers-Moll BJTs in `u273_reference/state_space`.
- Damped Newton and dense linear solve for offline nonlinear steps.
- A netlist loader with a deliberate guarded BJT policy.
- Calibration/report gates with promotion guarded by
  `CalibrationReport::canPromoteBoundary()`.
- Realtime DSP that is JUCE-independent and table/surrogate driven.

The blockers are not DSP blockers. They are scientific reference blockers:

- B11 is still represented by a finite Thevenin command port, not a complete
  active MNA subcircuit.
- B6 active devices are not fully topologically closed. B6 Ts2 and Ts4 have
  candidates; B6 Ts1, Ts3, Ts5, Ts6 are still symbolic or output-bound only.
- B11 active transistor orientations are not confirmed.
- `ActiveTopologyEvaluator` only evaluates the current B6 Ts2/Ts4 guarded
  candidates.
- The AC reference still rebuilds a specialized B6 small-signal fixture instead
  of linearizing the full loaded B6+B11 netlist around the converged operating
  point.
- Existing bench output keeps `canPromoteBoundary() = false`; AC residuals,
  THD target mapping, topology diagnostics, and strict audio gates do not yet
  pass.

## Source Hygiene

The Perplexity document is useful as a research checklist, but it contains
placeholder citation markers such as `[]` and `[][]`. Treat it as guidance, not
promotion evidence. Only facts backed by local schematics, netlist artifacts,
datasheets, direct measurements, or repeatable tests should be allowed into the
validation report.

## Step 1 Implementation Status

The current step-1 implementation is topology-safe only. It does not activate
B11 or replace the guarded Thevenin command source.

- `U273_DEBUG_CONFIG_001` marks the selected drawn/debug position as
  `PARTIAL_TOPOLOGY_SAFE_DC_CLOSURE`.
- S3/S4/S6/S7 contacts are encoded as explicit candidates, not promoted switch
  truth tables.
- B6 output topology is corrected around R36/R33/C20/R34/C21/U2 and guarded by
  a local passive KCL checkpoint.
- B11 has a passive candidate subcircuit in
  `topology_step1.b11_passive_candidate_subcircuit`; it is outside the official
  executable component array.
- The official loader still sees 88 executable components.
- An opt-in isolated B11 passive KCL probe can be generated with
  `node solver/export_b11_passive_candidate_experiment.js --enable-b11-passive-candidate-experiment`.
  It stamps only passive DC-visible candidate branches, clamps the printed B11
  voltages, and reports missing active currents. It is not BJT pin proof.
- Prior B11 exploratory scripts with conflicting assumptions are quarantined in
  `topology_step1.b11_prior_artifact_audit`; they cannot feed pin extraction,
  active stamping, or promotion until reconciled with the current step-1 reading.
- A direct printed-node B11 Ts1/Ts2 pin prefilter now rejects naive B/C/E
  permutations using only printed node voltages. This does not identify the
  real pins; it proves that route proof or hidden/intermediate nodes are needed
  before active stamping.
- The B11 R15/R16 reading is corrected to 1.2 kOhm, not 12 kOhm. The local
  R15/R16/R13 KCL is therefore a strong partial candidate, not closed proof:
  `IR15 - IR16 - IR13` leaves about 0.378 mA to explain through Ts2 or adjacent
  routes.
- The full PDF image has now been extracted into visual evidence crops under
  `results/pdf_evidence/`. `topology_step1.b11_pdf_topology_evidence_ledger`
  confirms the local routes R13 `N9 -> N05`, R16 `N9 -> reference`, Ts2's
  terminal graph around `N145/N9/R14/C6`, C8/C9 as DC-open dynamic branches,
  C5 as the series route from `N05` into `R7/R8/S6`, and C11 as rectifier/filter
  storage. A visible S6/R1/R2 boundary conductor candidate is also confirmed,
  but it is not enough to replace the software `CMD` boundary. This is still not
  active closure: Ts1/Ts2 B/C/E, the missing active currents, S6/S7 full truth
  tables, active B11 output impedance, and the detector/C11-to-B6-CMD transfer
  remain guarded.
- `topology_step1.b11_pdf_active_hypothesis_constraints` converts the PDF
  evidence plus passive KCL into rejection constraints for the next evaluator:
  N145 needs about 7.92 mA sunk, N9 needs about 7.54 mA sourced, and N05 needs
  about 4.14 mA sourced. These rows are explicitly rejection-only; they do not
  assign B/C/E or enable Ebers-Moll stamps.
- `topology_step1.b11_retranscription_crosscheck` records the UTF-8 GPT
  retranscription as a secondary checklist only. It agrees with the PDF ledger
  on R13, R16, R15, Ts2 graphic terminals, C5, C8/C9, and the C11 value, but it
  also exposes the next unproven routes: B11 R31/D1/D2/NCMD, C7, S6/S7
  pre-emphasis values, and whether the C8/C9/C7 driver-side terminals are
  separate nodes or a common conductor. It does not stamp B11, assign B/C/E, or
  replace the Thevenin CMD path.
- The 2026-05-21 GPT cross-check has now been reconciled against the local PDF
  crops. `topology_step1.b11_pdf_topology_evidence_ledger` additionally records
  R31 `B11_ND2_BOT_RAW -> 51k -> B11_NCMD_LOCAL`, D2 SSD55, D1 ZL10, and C7
  `25u -> B11_N215` as visual endpoint evidence. These rows are still guarded:
  diode polarity/model parameters, the `C7/C8/C9` driver-side common-node
  hypothesis, S7 17/18/19 routing, and the distinction between local B11 NCMD
  and the software B6 `CMD` boundary remain unresolved. The passive candidate
  subcircuit now contains 20 disabled review-only components; the official
  executable netlist still has 88 components.
- The current S6/S7 work is explicitly represented as
  `topology_step1.b11_s6_s7_switch_matrix_candidate`. It records the passive
  pre-emphasis values `R3=R4=7.5k`, `R5=15k`, `R6=3.9k`, `C1=C2=3n`,
  `C3=3.3n`, `C4=3n`, plus guarded `R1=3k` and `R2=1k` user candidates. It
  forbids shortcut labels such as `S6 = mode_limiter` or
  `S7 = mode_compressor`; every mechanical position still needs closed/open
  contacts, left/right nodes, effect on pre-emphasis, and effect on
  `CMD/D1/D2` before promotion.
- The dynamic driver-side capacitors now encode the strongest guarded reading:
  C7 and C8 use `B11_NDRV_C78_CANDIDATE`, while C9 uses
  `B11_NDRV_C9_CANDIDATE`. `NDRV_C78` must not be merged with `NDRV_C9`
  without visible continuous-conductor proof.
- D2 and D1 now carry polarity candidates only. D2 records
  `anode_candidate=B11_N20_D2_TOP` and `cathode_candidate=B11_ND2_BOT_RAW`;
  D1 ZL10 records `anode_candidate=B11_NZENER_OUT_18_19`,
  `cathode_candidate=B11_NCMD_LOCAL`, and `BV=10V` as a model candidate. Both
  keep `spice_polarity_promoted=false`.
- `topology_step1.b11_d1_d2_polarity_experiment` is now an opt-in guarded
  experiment. It sweeps the graphical and reversed D1/D2 polarities, applies
  S7 limiter/compressor function as rejection logic, and produces one
  functional-pass candidate for future guarded MNA stamping. It still keeps
  `spice_polarity_promoted=false`, keeps `B11_NCMD_LOCAL` separate from the
  B6 `CMD` boundary, and does not alter the official 88-component executable
  netlist.
- `topology_step1.b11_pdf_text_functional_evidence` records the UTF-8 text
  analysis as functional evidence only. It confirms B11 as a feedback
  `Regelverstaerker`, D1/ZL10 as the limiter threshold zener, S7 as the
  compressor-mode zener bypass function, S6 as pre-emphasis switching, the
  Siemens empirical gain-bridge diode branch law `UD_mV = 308 * ID_uA^0.16`,
  a typical 25 mV audio signal at the gain-control bridge, and attack/release
  plausibility targets. It still does not promote S6/S7 contact truth tables,
  D1/D2 SPICE polarity, C11-to-CMD topology, BJT pinout, active B11, or the
  replacement of the Thevenin command port.
- `topology_step1.b11_gpt_reponse_2_promotion_audit` records the 2026-05-21
  external activation proposal as an audit, not promotion evidence. It accepts
  D1/D2 polarity and S7 bypass behavior only as guarded constraints, keeps C11
  out of `NCMD` KCL, rejects the proposed `C7 -> V24` change because the local
  crop still supports `C7 -> B11_N215`, and rejects adding official B11
  components until S6/S7 contacts, diode experiments, active transistor pins,
  and a real B11-to-B6 `CMD` KCL equation are proven.
- `topology_step1.b11_scientific_activation_research_ledger` records the first
  source-backed activation research pass. It keeps the MNA basis tied to
  Ho/Ruehli/Brennan, diode/BJT parameters tied to SPICE/ngspice-style model
  requirements, and nonlinear state-space reduction tied to Holters/Zoelzer as
  a later realtime step. The practical result is strict: implement a guarded
  zener-capable diode model before any D1 ZL10 experiment. That model
  capability now exists in the reference component model, but D1 ZL10 is still
  not stamped. Treat ZL10 as a
  10 V breakdown candidate, SSD55 as weak identity-only data, OA154Q as audio
  bridge evidence only, and SST116/SST117 as wide-bounded NPN hypotheses. This
  ledger does not stamp B11 or replace the Thevenin command port.

## Work Package 1: Replace Thevenin B11 With Active MNA B11

Goal: remove the scientific dependency on
`B11_S6_S7.VB11_S6_S7 + RB11_S6_S7_CMD` as the main command-path model.

Required outcome:

- Build a B11 active subcircuit in the orchestrated netlist.
- Include detector path, ZL10/SSD55/OA diode behavior, S7 limiter/compressor
  modes, S6 switched RC network, C11, B11 transistors, and the real output node
  that drives B6 `CMD`.
- Keep switch and component uncertainties explicit until proven.
- Preserve the finite Thevenin port only as a regression fixture, not as the
  primary closure target.

Primary files/artifacts:

- `results/u273_netlist.json`
- `results/u273_netlist.txt`
- `modules/u273_reference/src/state_space/U273NetlistLoader.cpp`
- `modules/u273_reference/include/u273/reference/state_space/U273NetlistLoader.h`

Exit criteria:

- The loaded B6+B11 netlist has an explicit CMD KCL equation where B11 and the
  B6 bridge are connected through real circuit elements.
- The loader report no longer depends on the Thevenin B11 source for the active
  closure path.

## Work Package 2: Prove BJT Topology Before Stamping

Goal: identify collector/base/emitter orientation for every active B6/B11
device before treating it as a physical model.

Required outcome:

- B6: close Ts1, Ts3, Ts5, Ts6; retain or reject Ts2/Ts4 candidates with
  explicit evidence.
- B11: define candidates for all active B11 transistors and prove or reject
  B/C/E orientation.
- Store every hypothesis as data, including rejected hypotheses and rejection
  reasons.

Candidate rejection rules:

- Impossible forward bias: VBE outside the configured plausible range.
- Impossible active-region voltage: VCE below minimum for the expected mode.
- Local KCL imbalance above tolerance.
- AC response worsens against golden or fixture constraints.
- Transient behavior fails to converge or becomes physically unstable.
- Calibration requires a parameter on a physical bound.

Primary files/artifacts:

- `modules/u273_reference/include/u273/reference/calibration/ActiveTopologyCandidate.h`
- `modules/u273_reference/include/u273/reference/calibration/ActiveTopologyEvaluator.h`
- `modules/u273_reference/src/calibration/ActiveTopologyEvaluator.cpp`
- `results/b6_active_hybrid_pi_inventory.json`
- `results/b11_active_hybrid_pi_inventory.json`

Exit criteria:

- `BjtStampPolicy::stampKnownTerminals` is only used for hypotheses that have
  passed DC/KCL/AC/transient checks.
- The closure candidate set has exactly one accepted topology per active device,
  or promotion remains impossible.

## Work Package 3: Generalize The Topology Evaluator

Goal: replace the current B6 Ts2/Ts4-only evaluator with a full B6/B11 topology
evaluation pipeline.

Required outcome:

- Load topology candidates from structured data instead of hard-coding two B6
  candidates.
- Evaluate all B6 and B11 active devices under the same operating point.
- Add AC-improvement and transient-stability checks to the evaluator output.
- Feed rejected topology reasons into `CalibrationReport`.

Primary files/artifacts:

- `modules/u273_reference/src/calibration/ActiveTopologyEvaluator.cpp`
- `modules/u273_reference/src/calibration/B6B11CalibrationRunner.cpp`
- `modules/u273_reference/include/u273/reference/calibration/CalibrationReport.h`
- `tests/main.cpp`

Tests to add:

- Candidate loader rejects missing or duplicate pins.
- Candidate evaluator rejects impossible VBE/VCE.
- Candidate evaluator rejects local KCL imbalance.
- Candidate evaluator records AC/transient rejection reasons.
- B6/B11 candidate inventory must be complete before promotion can pass.

## Work Package 4: Replace Partial AC/Transient References With Full Netlist

Goal: analyze the same loaded active netlist for DC, AC, and transient behavior.

Required outcome:

- DC: solve the full B6+B11 operating point with active BJTs included.
- AC: linearize the loaded circuit around that operating point instead of
  rebuilding a specialized B6 small-signal fixture.
- Transient: simulate the coupled B6+B11 DAE, including detector/C11/CMD
  dynamics, not a swept detector drive.

Primary files/artifacts:

- `modules/u273_reference/src/calibration/B6SmallSignalAcReference.cpp`
- `modules/u273_reference/src/calibration/LinearizedAcSolver.cpp`
- `modules/u273_reference/src/calibration/OperatingPointSolver.cpp`
- `modules/u273_reference/src/calibration/TransientReferenceSolver.cpp`
- `modules/u273_reference/src/calibration/B6B11CalibrationRunner.cpp`

Exit criteria:

- The same active netlist and parameter set are used for DC, AC, transient, and
  audio validation.
- AC and transient gates cannot pass by using a fixture that bypasses unresolved
  B6/B11 topology.

## Work Package 5: Promotion Gates And Realtime Projection

Goal: make promotion possible only after the offline reference is defendable.

Required outcome:

- Generate a calibration report JSON with DC, AC, transient, audio, topology,
  and identifiability evidence.
- Ensure no fitted parameter is on a physical bound.
- Ensure topology hypotheses are uniquely accepted and justified.
- Ensure C++ tests pass.
- Compare realtime against offline after offline validation, not before.

Primary files/artifacts:

- `modules/u273_reference/include/u273/reference/calibration/CalibrationReport.h`
- `modules/u273_reference/src/calibration/BoundedCalibrationSolver.cpp`
- `modules/u273_reference/src/calibration/B6B11CalibrationRunner.cpp`
- `modules/u273_dsp/src/U273DspEngine.cpp`
- `modules/u273_dsp/src/TableReductionRealtimeEngine.cpp`
- `modules/u273_dsp/src/FullActiveRealtimeEngine.cpp`

Exit criteria:

- `CalibrationReport::canPromoteBoundary()` returns true from repeatable test
  data.
- Only then add a new explicit boundary such as:

```text
fullActiveModelValidated
```

- The realtime path may then consume a table, reduced model, or other surrogate
  derived from the validated offline model.

## Required Golden Data

The project cannot prove U273 equivalence without comparable data. Minimum
dataset:

- DC node voltages for B6/B11 at idle and under representative command levels.
- CMD, detector/C11, and bridge-current measurements.
- S6/S7 contact-position truth table.
- AC Bode data for the audio path and sidechain path.
- Attack/release transients for limiter and compressor modes.
- THD+N versus level/frequency with measurement node and mode mapped to the
  project sonic target.

Without this data, the project can close an internally consistent hypothesis,
but it cannot honestly claim hardware-equivalent full-active U273 modeling.

## First Implementation Sprint

Recommended order:

1. Add a structured active-topology candidate inventory for B6 and B11.
2. Extend `ActiveTopologyEvaluator` to consume the inventory.
3. Add tests for candidate completeness and rejection reasons.
4. Add B11 active subcircuit data without deleting the Thevenin fixture.
5. Load the active B6+B11 netlist under an experimental closure mode.
6. Solve and gate DC first.
7. Replace the specialized B6 AC fixture with full-netlist linearization.
8. Add coupled transient validation.
9. Only after all offline gates pass, derive and test the realtime surrogate.

DC must stay first. If the DC operating point is wrong, AC, transient, and audio
fits can look convincing while remaining physically undefendable.
