# Maintaining & updating the fork

How to make future improvements and republish them. Read this before pushing —
the published repo has a deliberately non-standard branch layout (explained at
the bottom), and "just `git push`" will not behave the way you expect if you
forget it.

## TL;DR branch model

- **Local working repo:** `C:\Users\Robert\Programs\kicad-source` (this checkout).
- **Publish branch:** `publish-10.0.6` (local) → tracks `fork/local-ipc-fixes`
  (remote).
  This is the branch the public GitHub repo serves and that releases are cut from.
  **Do all ongoing work here.**
- `local-ipc-fixes` (local) is the original shallow-clone dev branch. It is kept
  for reference only; it cannot be pushed (shallow). Don't develop on it anymore.

So: **edit on `publish-10.0.6`, commit, `git push`** (upstream is already set).

## Routine update — docs / scripts only (no binary change)

```bash
git checkout publish-10.0.6
# edit dist/*, README, etc.
git add -A && git commit -m "..."
git push                      # → fork/local-ipc-fixes
```
No new release needed unless you want to refresh the attached zip.

## Update that changes the KiCad C++ (new/changed IPC handler, etc.)

1. **Edit the source** on `publish-10.0.6` (e.g. `eeschema/api/api_handler_sch.cpp`,
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
   (e.g. `v10.0.6-ipc.3`).

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

## Bumping the upstream base (e.g. 10.0.6 -> 10.0.7)

The fork's own commits are few and self-contained, so a base bump is a replay,
not a merge. Done once already (rc2 -> 10.0.6); the shape:

1. `git fetch origin` and check what moved:
   `git log --oneline <old-base>..origin/10.0 -- api/ common/api/ pcbnew/api/ eeschema/api/`
   Anything there may overlap the fork's patch, and some of it may make part of
   the patch redundant -- upstream landing a fix you carry is the good outcome.
2. Replay onto a **worktree** so the main checkout keeps serving protos to any
   running agent:
   `git worktree add -b rebase-<ver> ../kicad-source-rebase origin/10.0`
   then cherry-pick the fork commits in order. Expect conflicts only where
   upstream touched the same file; RouteTrack's include block is the usual one.
3. Build and TEST against a real board before publishing anything.
4. Re-create the published orphan import from the tested branch:
   ```bash
   git checkout --orphan publish-<ver> origin/10.0
   git commit -m "Import KiCad <ver> (upstream <sha>, version <ver>)"
   for c in $(git rev-list --reverse origin/10.0..rebase-<ver>); do git cherry-pick $c; done
   ```
   **Verify the trees match before pushing** -- this is the check that proves you
   are publishing the source you actually built:
   `git rev-parse publish-<ver>^{tree} rebase-<ver>^{tree}`  (must be equal)
5. `git push --force-with-lease fork publish-<ver>:local-ipc-fixes`, then cut the
   release. The force is expected: each base bump replaces the orphan history.

**Check the deploy set on every bump.** `manifest.json` ships every KiCad-built
binary, not just the patched ones, because `kicommon`/`kigal` are replaced
wholesale and the other components link them with no ABI guarantee. If upstream
adds a component, add it. Watch vcpkg runtime DLLs too: the 10.0.6 bump moved
ngspice 45.2 -> 46, and `_eeschema` links it.

## Why the branch layout is weird (background)

The original `kicad-source` checkout is a **shallow clone**. A shallow clone
can't push a complete pack to a fresh remote — git can't send history below the
shallow boundary, so GitHub rejects it with `index-pack failed / did not receive
expected object`. Rather than download all of upstream KiCad's history just to
republish it, we created the `publish` branch as an **orphan import**: one
squashed "Import KiCad 10.0.6" root commit holding the full source tree, with
the fork's own commits cherry-picked on top. That is complete corresponding
source (GPL-clean), much smaller, and pushable. Now that the remote has full
(non-shallow) history on that branch, ordinary incremental pushes work — which is
why ongoing work just happens on `publish-10.0.6`.

If you ever need true upstream history, fetch it into a fresh full clone of
KiCad and re-apply the fork commits there; don't try to unshallow this checkout
mid-stream.
