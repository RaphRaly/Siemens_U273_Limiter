# U73 Limiter / U273 Plugin

Vintage gain-cell limiter research plugin built with CMake and JUCE.

## Download Beta_1.0

Current public beta:

- [U273_Limiter_Beta_1.0_Windows_VST3.zip](releases/Beta_1.0/U273_Limiter_Beta_1.0_Windows_VST3.zip)
- Platform: Windows x64
- Format: VST3
- SHA256: `EEFC08BD26946040D64D47E64A98088A416BEF28A7DB494092493366AF7C7FF9`

### Install the VST3

1. Download and unzip `U273_Limiter_Beta_1.0_Windows_VST3.zip`.
2. Copy the extracted `U273 Limiter.vst3` folder to:

```text
C:\Program Files\Common Files\VST3
```

3. Rescan plugins in your DAW.

The plugin currently appears to hosts as `U273 Limiter`; the current GUI faceplate is marked `U73 Limiter`.

## Build From Source

Configure with Visual Studio 2019:

```powershell
cmake --preset vs2019-x64
cmake --build --preset debug
ctest --preset debug
```

Tests without JUCE/plugin:

```powershell
cmake --preset vs2019-x64-tests-only
cmake --build --preset debug-tests-only
ctest --preset debug-tests-only
```

The local project preset expects JUCE at:

```text
C:\Users\user\JuceProgram\JUCE
```

## Packages

- `u273_core`: stable contracts, units, parameter IDs, model boundary metadata.
- `u273_dsp`: realtime engine; no JUCE, no file I/O, no allocation in `process`.
- `u273_reference`: offline scientific reference boundary for DC/AC/transient work.
- `u273_plugin`: JUCE adapter for VST3, and AU on Apple builds.
- `u273_assets`: calibration tables, presets, golden files.
- `u273_tests`: unit and boundary tests.
