# kicad-ipc-fork (experimental)

Prebuilt KiCad 10 binaries with extended IPC API handlers for the
[live-view](https://github.com/rpg32/live-view) agent tooling.

⚠️ **EXPERIMENTAL / AGENT-GENERATED — UNVETTED.** These modifications were
produced through agentic (LLM-driven) coding and have not been reviewed by KiCad
maintainers. ABI-locked to one exact KiCad build; mixing with a different
install can crash or corrupt files. Use a VM/scratch install, back up projects.
Not affiliated with or endorsed by the KiCad project.

## Install
1. Install stock KiCad 10.0.
2. Extract this zip; from an elevated PowerShell run `./install.ps1`.
3. Restart KiCad; enable **Preferences → Plugins → Enable KiCad API**.
4. Revert any time with `./uninstall.ps1`.

## Contents
Matched binary set (`bin/`): kicommon, kiapi, kigal, _eeschema, eeschema.exe,
_pcbnew, pcbnew.exe, kicad-cli. See `manifest.json` and `README.md`.

## License & trademark
GPLv3+ (KiCad's license). Corresponding source: branch `local-ipc-fixes`.
"KiCad" is a trademark of The KiCad Project; this is an **unofficial, unaffiliated,
unendorsed** fork. The GPL covers the code, not the name. Report issues here, not
to KiCad.
