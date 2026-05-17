# Run locally

The project ships a single local "always demoable" gate at the root:

```powershell
./build_and_demo.ps1             # configure + tests + full build + Demo/ copy
./build_and_demo.ps1 -TestsOnly  # just the scientific reference (no JUCE)
./build_and_demo.ps1 -SkipCopy   # full build but no Demo/ refresh
./build_and_demo.ps1 -InstallVst3 # also install the VST3 into CommonProgramFiles\VST3
```

The script is fail-fast (`$ErrorActionPreference = 'Stop'`) and chains:

1. `cmake --preset vs2019-x64-tests-only`
2. `cmake --build --preset debug-tests-only`
3. `ctest --preset debug-tests-only --output-on-failure`
4. `cmake --preset vs2019-x64`
5. `cmake --build --preset debug`
6. `ctest --preset debug --output-on-failure`
7. `git diff --check` (skipped if the local checkout is not a git repository)
8. Copy the freshest `.vst3` from `build/vs2019-x64/` into `Demo/U273[_<hash>]_<timestamp>.vst3`
9. Copy the freshest Standalone `.exe` to `Demo/U273_Standalone_<timestamp>.exe`
10. When `-InstallVst3` is passed, also install the freshest `.vst3` into `$env:CommonProgramFiles\VST3\U273_<timestamp>.vst3` (directory created if missing)

Launch the Standalone by double-clicking `Demo/U273_Standalone_*.exe` -- JUCE provides a basic audio I/O dialog out of the box.

There is no remote CI yet -- this script is the local gate.

The audio gate inside `runOffline` stays strictly closed: promotion to
`FULL_ACTIVE_MODEL_PROMOTED` requires a realtime plugin THD bench that is
deferred to a separate sprint.
