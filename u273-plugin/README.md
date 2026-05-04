# Siemens U273 Plugin Scaffold

This directory materializes the UML architecture into CMake/JUCE packages.

Packages:

- `u273_core`: stable contracts, units, parameter IDs, model boundary metadata.
- `u273_dsp`: realtime engine. No JUCE, no file I/O, no allocation in `process`.
- `u273_reference`: offline scientific reference boundary for DC/AC/transient work.
- `u273_plugin`: JUCE adapter for VST3, and AU on Apple builds.
- `u273_assets`: calibration tables, presets, golden files.
- `u273_tests`: unit and boundary tests.

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

The scaffold points to local JUCE at `C:/Users/user/JuceProgram/JUCE`.
