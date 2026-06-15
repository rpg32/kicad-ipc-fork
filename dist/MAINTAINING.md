# Maintaining & updating the fork

How to make future improvements and republish them. Read this before pushing —
the published repo has a deliberately non-standard branch layout (explained at
the bottom), and "just `git push`" will not behave the way you expect if you
forget it.

## TL;DR branch model

- **Local working repo:** `C:\Users\Robert\Programs\kicad-source` (this checkout).
- **Publish branch:** `publish` (local) → tracks `fork/local-ipc-fixes` (remote).
  This is the branch the public GitHub repo serves and that releases are cut from.
  **Do all ongoing work here.**
- `local-ipc-fixes` (local) is the original shallow-clone dev branch. It is kept
  for reference only; it cannot be pushed (shallow). Don't develop on it anymore.

So: **edit on `publish`, commit, `git push`** (upstream is already set).

## Routine update — docs / scripts only (no binary change)

```bash
git checkout publish
# edit dist/*, README, etc.
git add -A && git commit -m "..."
git push                      # → fork/local-ipc-fixes
```
No new release needed unless you want to refresh the attached zip.

## Update that changes the KiCad C++ (new/changed IPC handler, etc.)

1. **Edit the source** on `publish` (e.g. `eeschema/api/api_handler_sch.cpp`,
   `pcbnew/api/api_handler_pcb.cpp`, or a `.proto` under `api/proto/`).

2. **Rebuild the affected binaries** (incremental — far faster than a clean build):
   ```powershell
   cmake --build build\msvc-win64-release --target eeschema   # or: pcbnew, kicommon, kicad-cli
   ```
   The build tree (`build/msvc-win64-release`) is already configured. If it is
   missing, follow upstream KiCad's `INSTALL.txt` to configure it once
   (vcpkg + MSVC on Windows). Expect a long first build; incrementals are minutes.

3. **Deploy locally and test** (elevated PowerShell, KiCad closed):
   ```powershell
   pwsh dist\install.ps1          # copies build output over your install, backs up originals
   ```
   Restart KiCad, enable the API (Preferences → Plugins → Enable KiCad API), and
   exercise the change.

4. **If you changed a `.proto`:** regenerate the live-view client bindings so the
   client message names match the new handler. From the live-view repo:
   ```bash
   bash .pi/extensions/kicad-adapter/regen-protos.sh
   ```
   That script prefers this local fork checkout (`KICAD_FORK_SRC`, default
   `C:\Users\Robert\Programs\kicad-source`), so it always matches what you just
   built. Update any client tool in `.pi/extensions/kicad-adapter/` that calls the
   new/renamed message, then test against a running KiCad.

   > ⚠️ The client bindings and the running KiCad binary **must** be generated
   > from the same proto. The whole `GetNets` vs `GetSchematicNetlist` saga was
   > exactly this drift — the client was generated from upstream KiCad master
   > while the binary implemented the fork's `GetNets`. Always regen from the fork.

5. **Repackage the binaries** into a release zip:
   ```powershell
   pwsh dist\package.ps1          # → dist/release/kicad-ipc-fork-<forkVersion>.zip
   ```
   If the change is significant, bump `forkVersion` in `dist/manifest.json` first
   (e.g. `v10.0.0-ipc.2`).

6. **Commit, push, and cut a release:**
   ```bash
   git add -A && git commit -m "eeschema: <what changed>"
   git push
   gh release create <forkVersion> "dist/release/kicad-ipc-fork-<forkVersion>.zip" \
       --repo rpg32/kicad-ipc-fork --target local-ipc-fixes \
       --title "<forkVersion>" --notes-file dist/RELEASE_NOTES.md
   ```

7. **If you renamed the repo or changed its owner**, update the URL in two places
   in the live-view repo: `PATCH_DOCS_URL` in
   `.pi/extensions/kicad-adapter/ipc.ts` and `BASE_URL` in
   `.pi/extensions/kicad-adapter/regen-protos.sh`.

## Why the branch layout is weird (background)

The original `kicad-source` checkout is a **shallow clone**. A shallow clone
can't push a complete pack to a fresh remote — git can't send history below the
shallow boundary, so GitHub rejects it with `index-pack failed / did not receive
expected object`. Rather than download all of upstream KiCad's history just to
republish it, we created the `publish` branch as an **orphan import**: one
squashed "Import KiCad 10.0.0-rc2" root commit holding the full source tree, with
the fork's own commits cherry-picked on top. That is complete corresponding
source (GPL-clean), much smaller, and pushable. Now that the remote has full
(non-shallow) history on that branch, ordinary incremental pushes work — which is
why ongoing work just happens on `publish`.

If you ever need true upstream history, fetch it into a fresh full clone of
KiCad and re-apply the fork commits there; don't try to unshallow this checkout
mid-stream.
