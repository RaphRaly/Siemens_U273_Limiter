# U273 Test Strategy

## Current Data Boundary

The strongest numeric source for the U273 is the Siemens technical page and
the matching Radiomuseum entry:

- input resistance: 10 kOhm
- output resistance: 30 Ohm
- load resistance: 300 Ohm
- frequency range: 40 Hz to 15 kHz
- nominal output: 1.55 Vrms, equivalent to about +6 dBu
- weighted noise distance: 70 dB relative to nominal output
- distortion: 0.5 percent before regulation, 1 percent at 40 Hz in regulation,
  0.5 percent from 1 kHz to 15 kHz in regulation
- limiter attack: about 0.5 ms
- compressor attack: about 1 ms
- release: 0.5 s to 1.5 s
- supply: 24 V DC, 50 mA

These values are regression constraints, not full calibration data.

## Peer Devices

The comparison set is limited to diode-bridge compressor/limiters with similar
line-level studio/broadcast intent:

- AMS Neve 2254/R: original 2254 series from 1969, 10 kOhm input, 80 Ohm
  source impedance, 20 Hz to 20 kHz within 1 dB, diode-bridge gain-control
  family.
- AMS Neve 33609/N: diode-bridge compressor/limiter, 10 kOhm input, 20 Hz to
  20 kHz, fast limiter attack around 2 ms, fast compressor attack around 3 ms.

Peer data is used only as a plausibility envelope. It must not override Siemens
data for U273 behavior.

## Added Unit Tests

The scaffold tests now include:

- U273 electrical spec capture: impedances, bandwidth, +6 dBu conversion,
  weighted noise conversion, 24 V / 50 mA power.
- Siemens four-point distortion formula for bridge current samples.
- Peer plausibility envelope against Neve 2254/R and 33609/N.
- Detector-envelope attack/release response at the Siemens time constants.
- JS golden-file pinning for DC, AC, and transient result summaries.
- C++ `u273_netlist.json` loader inventory checks against the orchestrated
  component set.
- First implicit DAE convergence on the loaded guarded U273 netlist.
- BJT calibration-path checks that known-terminal active hypotheses can be
  stamped without changing the default guarded boundary.
- FFT audio checks for low-level THD and 40 Hz to 15 kHz band flatness.
- Existing DSP safety and state-space solver tests.

The test suite intentionally distinguishes three layers:

- `PASS_WITH_GUARDED_BOUNDARIES`: project/global schematic chain still guarded.
- `FULL_ACTIVE_MODEL_UNVERIFIED`: component-level or realtime analog engine is
  active but not yet fully calibrated.
- final U273 equivalence: not claimed until active B6/B11 closure and golden
  audio/reference comparisons pass.

## Sources

- Siemens U273 Radiomuseum technical entry:
  https://www.radiomuseum.org/r/siemens_begrenzer_kompressor_verstaerker_u273.html
- Siemens U273 PDF used locally in the project:
  `C:/Users/user/Desktop/Siemens U273/Siemens_U273_Limiter.pdf`
- AMS Neve 2254/R User Manual:
  https://www.ams-neve.com/wp-content/uploads/2023/01/2254R_User_Manual_Iss2_4-1.pdf
- AMS Neve 33609/N User Manual:
  https://www.ams-neve.com/wp-content/uploads/2022/01/33609N1.0usermanual.pdf
