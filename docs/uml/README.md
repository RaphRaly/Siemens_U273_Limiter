# U273 Plugin UML Architecture

Status: draft architecture source.

These PlantUML files document the first architecture pass for the Siemens U273 C++/JUCE VST3/AU plugin.

Files:

- `01_component_overview.puml`: high-level component boundaries.
- `02_package_dependencies.puml`: package/module dependency rules.
- `03_class_core.puml`: first core class model.
- `04_sequence_audio_callback.puml`: realtime audio callback sequence.
- `05_sequence_parameter_update.puml`: UI/DAW parameter update sequence.
- `06_state_plugin_lifecycle.puml`: plugin/DSP lifecycle statechart.
- `07_state_model_boundary.puml`: scientific/realtime model boundary statechart.

Rules:

- `u273_dsp` must not depend on JUCE UI or file/preset systems.
- `processBlock` must not allocate, lock, log, or perform I/O.
- Scientific reference code is offline and must not enter the audio thread.
- UML documents contracts and boundaries; implementation must follow these dependencies.

