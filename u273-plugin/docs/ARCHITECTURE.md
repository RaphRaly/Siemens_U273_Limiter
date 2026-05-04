# U273 Plugin Architecture

The C++ scaffold follows the UML package rule:

```text
u273_core <- u273_dsp <- u273_plugin
u273_core <- u273_reference
u273_assets is read by plugin/reference/tests, not by the realtime process loop.
```

Realtime boundary:

- `u273_dsp::U273DspEngine::process` receives a plain `ProcessContext` and
  immutable `ParameterSnapshot`.
- It has no JUCE dependency.
- It performs no file I/O, logging, heap allocation, locks, or host calls.

Scientific boundary:

- `u273_reference` is offline-only.
- The current declared status is `PASS_WITH_GUARDED_BOUNDARIES`.
- The realtime B6 bridge is intentionally named a guarded surrogate until the
  active transistor model is closed and calibrated against golden data.
- The component-level state-space/DAE solver lives under
  `u273_reference/state_space` and currently declares
  `FULL_ACTIVE_MODEL_UNVERIFIED`; it is a scientific reference path, not the
  realtime plugin path.
