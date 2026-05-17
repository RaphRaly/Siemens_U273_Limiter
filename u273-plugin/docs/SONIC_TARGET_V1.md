# U273 Sonic Target V1

## Purpose

This file defines the first measurable sonic target for the Siemens U273 plugin.
It is a calibration contract, not a claim of final U273 equivalence.

The target separates:

- direct Siemens or schematic facts that can be used as hard gates
- component priors that constrain the model without pretending to close unknowns
- inferred texture targets used to start listening and bench calibration
- data that was searched for but not found publicly

Realtime promotion must not depend on a single THD number. The model must pass
DC, AC, transient, identifiability, distortion, and audio behavior gates before
the guarded boundary can be relaxed.

## Provenance Levels

| Level | Meaning | Use |
|---|---|---|
| `siemens_primary` | Siemens text, Siemens schematic, or a matching technical entry | Hard target unless contradicted by better primary data |
| `schematic_derived` | Derived from the U273 schematic or local netlist transcription | Hard structural constraint, numeric tolerance depends on unknowns |
| `component_datasheet` | Manufacturer or tube/data listing for a specific part | Prior or bounded calibration range |
| `comparable_device` | Similar diode-bridge/broadcast compressor | Plausibility envelope only |
| `inferred_modeling_assumption` | Engineering inference from topology and available facts | Soft target, must be labeled in outputs |
| `not_found` | Searched for but no public reliable data found | Must not be silently replaced by a guess |

## Direct Siemens Targets

| Target | Value | Provenance | Gate role |
|---|---:|---|---|
| Input resistance | 10 kOhm | `siemens_primary` | Hard electrical target |
| Output resistance | 30 Ohm | `siemens_primary` | Hard electrical target |
| Load resistance | 300 Ohm | `siemens_primary` | Hard test load |
| Frequency range | 40 Hz to 15 kHz | `siemens_primary` | Audio band gate |
| Nominal output | 1.55 Vrms, about +6 dBu | `siemens_primary` | Level reference |
| Weighted noise distance | 70 dB relative to 1.55 Vrms | `siemens_primary` | Noise floor target |
| Distortion before regulation | 0.5 percent from 40 Hz to 15 kHz | `siemens_primary` | Output THD hard ceiling |
| Distortion in regulation at 40 Hz | 1.0 percent | `siemens_primary` | Output THD hard ceiling |
| Distortion in regulation from 1 kHz to 15 kHz | 0.5 percent | `siemens_primary` | Output THD hard ceiling |
| Limiter attack | about 0.5 ms | `siemens_primary` | Ballistics target |
| Compressor attack | about 1 ms | `siemens_primary` | Ballistics target |
| Release | 0.5 s to 1.5 s | `siemens_primary` | Ballistics target |
| Supply | 24 V DC, 50 mA | `siemens_primary` | Operating condition |
| Diode bridge signal range | about 10 mV to 50 mV internal signal | `siemens_primary` | Internal bridge operating range |
| Diode bridge example | 25 mV signal, 1 V control, K = 0.18 percent | `siemens_primary` | Internal bridge sanity check |

THD conversions used by the bench assets:

| THD | dB |
|---:|---:|
| 0.18 percent | -54.89 dB |
| 0.5 percent | -46.02 dB |
| 1.0 percent | -40.00 dB |
| 1.5 percent | -36.48 dB |

The Siemens diode-branch law transcribed for the bridge at 25 degC is:

```text
U_D = 308 * I_D^0.16
```

where `U_D` is in mV and `I_D` is in uA. The inverse form is:

```text
I_D = 2.85e-16 * U_D^6.25
```

The differential resistance approximation is:

```text
r_d = 48.3 * I_D^-0.84 kOhm
```

The stated useful current range is about 2 uA to 500 uA with less than 5 percent
error for the empirical law.

## Missing Direct Data

The public material found so far does not provide a complete musical target.
These items must remain inferred or measured from hardware:

- exact threshold scale in dBu
- exact ratio curves and knee shape
- maximum clean gain reduction before obvious overload
- harmonic breakdown H2 through H5 by level, frequency, and gain reduction
- IMD data
- THD versus gain reduction matrix
- transformer ratios and magnetic saturation behavior for U1, U2, and B11 Ue
- tolerance spread across OA154Q and SSD55 devices in the actual bridge
- sidechain ripple spectrum versus release setting

## THD And Texture V1

The direct Siemens output THD limits are stricter than the current offline
internal clipper golden. The old `-60 dB` scalar is not a U273 output target and
must not be promoted as such. The current `-21 dB` diode-clipper derivation is
useful only for a specific internal soft-clipper node and drive condition.

For V1, output THD targets are split into hard Siemens ceilings and inferred
musical ranges:

| Scenario | Frequency | Gain reduction | Target THD |
|---|---:|---:|---:|
| Nominal output, no regulation | 1 kHz | 0 dB | 0.25 to 0.55 percent |
| Nominal output, no regulation | 40 Hz | 0 dB | 0.40 to 0.80 percent |
| Light regulation | 1 kHz | 3 dB | 0.35 to 0.60 percent |
| Moderate regulation | 1 kHz | 6 dB | 0.45 to 0.80 percent |
| Strong regulation | 1 kHz | 10 dB | 0.60 to 1.10 percent |
| Heavy regulation | 1 kHz | 15 dB | 0.70 to 1.20 percent |
| Low frequency regulation | 40 Hz | 6 dB | 0.80 to 1.50 percent |
| Low frequency regulation | 40 Hz | 10 dB | 0.90 to 1.50 percent |
| Low-mid regulation | 100 Hz | 6 dB | 0.50 to 1.00 percent |
| High frequency regulation | 5 kHz to 15 kHz | 6 dB | 0.25 to 0.70 percent |

Harmonic texture expectation:

- a well-balanced bridge should lean odd-order, especially H3
- H2 should rise with diode mismatch, transformer asymmetry, or DC imbalance
- H5 should normally stay below H3 except under overload
- low frequency regulation should expose more ripple and transformer behavior

These texture rows are soft targets until hardware measurements exist. They
should guide listening, not mask a failed Siemens output THD gate.

## Component Priors

The bridge is not a generic 1N4148 clipper. The B6 diode bridge uses germanium
and silicon parts in each branch. The available schematic transcription lists:

- D1: OA154Q
- D2: SSD55
- D3: SSD55
- D4: OA154Q

OA154Q is a germanium point-contact quartet. Public data lists about 6 mA
forward current at 1 V, about 10 uA reverse current at 10 V, 20 mA maximum
current, 50 V maximum voltage, and a 100 degC junction limit. Shockley fits from
only one forward point are weak priors, not final parameters.

SSD55 public data is insufficient for a reliable closed model. It must remain a
calibrated silicon-diode prior until a primary datasheet or hardware measurement
is added. Do not silently substitute 1N4148 as the final target.

## Promotion Guidance

Before a realtime model can claim this sonic target:

- output THD gates must use device-output targets, not internal-node clipper
  targets
- every THD row must record measurement node, frequency, level, gain reduction,
  and regulation mode
- inferred rows must stay labeled as inferred in reports
- component unknowns must stay bounded and identifiable
- the realtime implementation must be no-NaN, bounded-cost, and oversampled when
  nonlinear bridge distortion is audible

## Source Set

- Siemens U273 schematic and technical PDF:
  https://gyraf.dk/schematics/Siemens_U273_Limiter.pdf
- Radiomuseum Siemens U273 technical entry:
  https://www.radiomuseum.org/r/siemens_begrenzer_kompressor_verstaerker_u273.html
- Radiomuseum OA154:
  https://www.radiomuseum.org/tubes/tube_oa154.html
- Radiomuseum OA154Q:
  https://www.radiomuseum.org/tubes/tube_oa154q.html
- Radiomuseum SSD55:
  https://www.radiomuseum.org/tubes/tube_ssd55.html
- DAFx 2025 diode VCA paper:
  https://www.corianderpines.org/download/dafx25_diode_vca.pdf
- AMS Neve 2254/R user manual:
  https://www.ams-neve.com/wp-content/uploads/2023/01/2254R_User_Manual_Iss2_4-1.pdf
- AMS Neve 33609/N user manual:
  https://www.ams-neve.com/wp-content/uploads/2022/01/33609N1.0usermanual.pdf
- EMT 156 manual:
  https://steampoweredradio.com/pdf/emt/manuals/EMT%20PDM-Compressor%20EMT%20156%20and%20EMT%20156%20TV%20Manual.pdf
