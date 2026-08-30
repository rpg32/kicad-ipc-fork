# kicad-ipc-fork (experimental)

Prebuilt KiCad 10 binaries with extended IPC API handlers for the
[live-view](https://github.com/rpg32/live-view) agent tooling.

**v10.0.6-ipc.2** — rebased from 10.0.0-rc2 onto **KiCad 10.0.6**, picking up
1,818 upstream commits including 36 that touch the IPC API (`GetConnectedItems`,
`FlipItems`, `GetItemsByNet`/`ByNetClass`, per-pad/via teardrop settings, zone
corner settings and layer overrides, dimensions, embedded files).

New in the fork this release:

- **`DragItems`** — drag existing copper (a track, arc, via, or a whole footprint
  via `DM_COMPONENT`) with KiCad's push-and-shove engine, headlessly. The
  counterpart to `RouteTrack`, which only places copper that does not yet exist.
  Unlike stock `InteractiveMoveItems` it does not hand control to the GUI move
  tool and wait for a human. Commits only a verified result — connectivity must
  not regress and nets near the drag must not gain a clearance violation — and
  otherwise reverts the whole shove. Reports whether the drag actually reached
  the requested point, since PNS clamps at an obstacle and would otherwise report
  a clamped position as plain success.
- **`RouteTrack`** now tears down its router on every early return (it leaked one
  router and interface per failed route, which is a hot path on a congested board).

⚠️ Stock KiCad exposes no router API on any branch, so `RouteTrack` and
`DragItems` exist only here.

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
Matched binary set (`bin/`) — **every KiCad-built component, not only the patched
ones**: kicommon, kiapi, kigal, _eeschema, eeschema.exe, _pcbnew, pcbnew.exe,
kicad_3dsg, kicad.exe, kicad-cli, _cvpcb, _gerbview, gerbview.exe, _pl_editor,
pl_editor.exe, _pcb_calculator, pcb_calculator.exe, bitmap2component, _kipython,
plus ngspice.dll. `kicommon`/`kigal` are replaced wholesale and the rest link
them with no ABI guarantee, so a partial overlay would leave a mixed install —
`kicad.exe` itself is in that set. See `manifest.json` and `README.md`.

## License & trademark
GPLv3+ (KiCad's license). Corresponding source: branch `local-ipc-fixes` (a squashed import of KiCad
10.0.6 with the fork's commits replayed on top; the published tree is verified
identical to the tree these binaries were built from).
"KiCad" is a trademark of The KiCad Project; this is an **unofficial, unaffiliated,
unendorsed** fork. The GPL covers the code, not the name. Report issues here, not
to KiCad.
