# Demo drop zone

`build_and_demo.ps1` copies the latest compiled `.vst3` here after a successful
full build. The contents of this folder (except this README and `.gitkeep`)
are intentionally ignored by git — it is a local "always demoable" surface,
not an artefact archive.

Each copy is stamped `U273[_<git-hash>]_<YYYYMMDD-HHMMSS>.vst3` so the latest
demo is unambiguous.

To refresh the demo from a fresh checkout:

```powershell
./build_and_demo.ps1
```

To verify only the scientific reference (no JUCE plugin build):

```powershell
./build_and_demo.ps1 -TestsOnly
```
