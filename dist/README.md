# KiCad IPC Fork — experimental extended IPC API for agent tooling

This is a **fork of KiCad 10** that adds IPC (Inter-Process Communication) API
handlers the stock build does not ship. It exists to support the
[live-view](https://github.com/rpg32/live-view) agent tooling, whose KiCad
schematic/PCB **write and routing** tools talk to a running KiCad over its IPC
socket.

---

## ⚠️ Experimental / agent-generated — read this first

**These modifications were produced experimentally through agentic (LLM-driven)
coding, and should be treated as unvetted and of questionable quality.** They
have not been reviewed or accepted by the KiCad project. They were not written
or audited by KiCad maintainers. Concretely, that means:

- The C++ may not follow KiCad conventions, may leak, or may crash.
- The IPC message contracts drift from upstream in places (see *Known issues*).
- Binary releases are ABI-locked to one exact KiCad build; mixing them with a
  different KiCad install can crash or corrupt files.
- **Do not use on production designs or a machine you can't afford to reset.**
  Prefer a VM or a scratch KiCad install. Back up your projects.

This fork is published mainly to (a) satisfy the GPL source-offer obligation for
the binaries the live-view project distributes, and (b) let curious people
reproduce the setup. It is not a supported KiCad variant.

---

## What it changes

On top of upstream KiCad (base commit `f492b347`, version string `10.0.0`),
branch `local-ipc-fixes` adds (~2,400 lines):

| Area | Change |
|------|--------|
| `eeschema/api/api_handler_sch.cpp` | IPC serialization + CRUD handlers for 13 schematic item types (symbols, wires, labels, junctions, no-connects, sheets, text, etc.) |
| `pcbnew/api/api_handler_pcb.cpp` | `RouteTrack` handler exposing the PNS push-and-shove router over IPC; API-created vias net-locked so planes don't absorb them |
| `api/proto/schematic/` + `common/api/` | Schematic protobuf messages and type-registry mappings |

The matched binary set that must be installed together (shared ABI):
`kicommon.dll`, `kiapi.dll`, `kigal.dll`, `_eeschema.dll`, `eeschema.exe`,
`_pcbnew.dll`, `pcbnew.exe`, `kicad-cli.exe`.

## Install (prebuilt binaries, Windows)

1. Install **stock KiCad 10.0** first (this fork patches over it).
2. Download the release zip and extract it.
3. From an **elevated** PowerShell:
   ```powershell
   ./install.ps1                      # default: C:\Program Files\KiCad\10.0\bin
   ./install.ps1 -KiCadDir "D:\..."   # custom install
   ```
   The installer backs up each original as `<file>.original`, copies the
   patched binaries in, and writes a `.live-view-patch.json` marker.
4. Restart KiCad. Enable the API: **Preferences → Plugins → Enable KiCad API**.

To revert:
```powershell
./uninstall.ps1
```
This restores every `*.original` backup.

> The installer warns (and stops, unless `-Force`) if your KiCad version string
> differs from the fork's base. Note the string alone does **not** prove ABI
> compatibility — KiCad does not promise a stable DLL ABI across builds. The
> only fully safe target is the exact matching upstream build.

## Build from source

KiCad is a large C++ project; expect a long build and substantial dependencies.

```bash
git clone <this-repo-url> kicad-source
cd kicad-source
git checkout local-ipc-fixes
# Configure + build per upstream KiCad's INSTALL.txt (vcpkg/MSVC on Windows).
# Then package the matched binary set:
pwsh dist/package.ps1
```

`dist/package.ps1` collects the binaries listed in `dist/manifest.json` from the
build tree into a release zip alongside the install scripts. To update the fork
and cut new releases, see **[`MAINTAINING.md`](MAINTAINING.md)**.

## API surface notes

- **Schematic netlist (`GetNets`).** This fork names the handler
  `GetNets`/`GetNetsResponse` (upstream KiCad uses a different name). The
  live-view client generates its protobuf bindings from **this fork's** protos
  (see `regen-protos.sh`), so the names match and `kicad_sch_get_nets` calls it
  directly over IPC. Earlier client builds generated from upstream KiCad master
  and hit `no handler available` — if you regenerate bindings, always source them
  from the fork. `GetNetsResponse` returns net **name + code** only; per-pin
  connection lists are not exposed (extending the handler to include nodes would
  be a future change — see `MAINTAINING.md`).
- The fork also adds `GetSheetHierarchy` under a fork-specific name; same
  generate-from-the-fork rule applies.
- Several stock IPC reads remain unimplemented in this build
  (`GetBoundingBox`, `GetPageSettings`, `GetTitleBlockInfo`, `RefreshEditor`).

## License

KiCad is **GPLv3 (or later)**. This fork is likewise GPLv3+. You may use, modify,
and redistribute it under those terms. The corresponding modified source is this
repository, branch `local-ipc-fixes`. See the upstream `LICENSE.*` files.

## Trademark & affiliation

"KiCad" and the KiCad logo are **trademarks of The KiCad Project / Kicad Services
Corporation**. This repository is an **independent, unofficial fork**. It is **not
produced, endorsed, sponsored, or supported by the KiCad project**, and the
maintainers are not affiliated with it.

The GPL covers the *code*; it does **not** grant rights to the KiCad name or logo.
These binaries are provided only for the experimental tooling described above and
identify themselves as "KiCad" solely because that string is baked into the
upstream sources — that is **not** a claim of being official KiCad. If you
redistribute modified builds more broadly, review KiCad's trademark policy
(<https://www.kicad.org/about/trademarks/>) and consider rebranding the binaries.
Report issues here, **never** to the KiCad project.
