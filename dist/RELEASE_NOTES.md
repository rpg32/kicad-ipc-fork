# kicad-ipc-fork (experimental)

Prebuilt KiCad 10 binaries with extended IPC API handlers for the
[live-view](https://github.com/rpg32/live-view) agent tooling.

**v10.0.6-ipc.12** — two fixes for footprints placed over the API.

- **Fix: a paste left its commit open.** `ParseAndCreateItemsFromString` on a board took
  the client's commit and never closed it when the client had not opened one itself. The
  pasted items were on the board but outside the undo history until that client's next
  write, which then pushed both changes as one undo step; the client's next `BeginCommit`
  was refused ("already has a commit in progress"); and a commit it dropped next took the
  paste with it. A paste outside a `BeginCommit` is now pushed as an undo step of its
  own, as `CreateItems` does. Inside a client's commit nothing changes.
- **Fix: `UpdateItems` on a footprint dropped its link to the schematic symbol.** The
  symbol path, sheet name and sheet file of the footprint in the request are now applied,
  so a footprint placed over the API can be tied to its symbol (Update PCB from Schematic
  otherwise sees it as an extra footprint). A request that carries no path leaves the
  link as it is.

**v10.0.6-ipc.11** — the last dialogs a client had to drive become requests, and two KiCad defects that made API edits fail are fixed.

- **Fix: edits inside a footprint reached the undo copy.** Every commit copies the
  footprint it changes as its undo image; the copy still names the board as its parent,
  and `FOOTPRINT::Add` entered the copy's pads, fields and graphics into the board's
  item-by-id cache in place of the live ones. After the first change inside a footprint,
  `GetItemsById` returned stale items for its other children and `UpdateItems` on them
  answered OK while changing nothing on the board (for example: teardrops or nets set on
  the pads of one footprint landed on the first pad only). The cache now takes children
  only from a footprint that is itself indexed on the board.
- **Fix: `SetNetClasses` ended the editor.** Deserializing a net class emptied its list of
  constituent classes, and the next lookup of the class name read past the end of it. An
  explicit net class is its own single constituent, as its constructor sets it up.
- **`ChangeFootprint`** (new, `board_commands.proto`): change one footprint to another
  library footprint, as the Change Footprint dialog does with its default options. Same
  place, side and angle; reference, value, id and the pads' nets are kept. The response
  lists the pads' nets and any connection the new footprint has no pad for.
- **`CleanupTracksAndVias`** (new): Tools > Cleanup Tracks and Vias with the dialog's
  options, an optional net filter and a dry run; returns each finding with its place.
- **`GetCustomDesignRules` / `SetCustomDesignRules`** (new): the board's `.kicad_dru`.
  Setting checks that the text parses first (a syntax error is refused with its line and
  column, and nothing is written), writes the file and rebuilds the rules the DRC and the
  router work from.
- **`GetNetClassPatterns` / `SetNetClassPatterns`** (new): which nets belong to which net
  class, by pattern. Setting also applies the net classes to the board's nets and rebuilds
  the design rules, as closing Board Setup does; send it after `SetNetClasses` to make
  changed classes take effect on an open board.
- **`GetTitleBlockInfo` / `SetTitleBlockInfo` in the Schematic Editor** (the board had
  them): the title block of the sheet shown. Before, the schematic answered "no handler".
- Regenerate client bindings from this release's protos (new messages only).

**v10.0.6-ipc.10** — KiCad's own geometry for a schematic sheet, changing a symbol and the board's design constraints over IPC.

- **`GetSheetGeometry`** (new, `schematic_commands.proto`): for the sheet shown, each
  symbol's body box, its pins (number, name, electrical type, the point a wire connects
  to, alternate functions) and the box every field occupies, plus the boxes of labels,
  text, sheets and the other non-wire items. A client can wire to a pin without saving
  and parsing the file, and can judge whether a drawing is readable (texts overlapping,
  wires across a symbol) from KiCad's symbol transforms and font metrics, not its own.
- **Change a symbol:** `UpdateItems` on a `SchematicSymbol` whose library identifier
  differs changes it to that library symbol, as the Change Symbols dialog does for one
  symbol: body and pins come from the new symbol; position, unit and field texts stay.
  An unknown symbol or one with too few units is refused (`ISC_INVALID_DATA`).
- **Fields placed as the editor places them:** a symbol created over IPC, or turned or
  mirrored by `UpdateItems`, has its automatically placed fields re-placed for the new
  orientation (when the editor's autoplace setting is on), as interactive placement and
  rotate do. Before, the library's field positions were only rotated with the symbol,
  which put Reference and Value on or against the body of a rotated part. Fields a user
  or a client positioned are left where they are.
- **`GetBoardDesignConstraints` / `SetBoardDesignConstraints`** (new,
  `board_commands.proto`): the minimums of Board Setup > Design Rules > Constraints.
  Setting changes only the constraints present in the request, marks the board modified,
  and rebuilds the rules the DRC and the router work from, as closing Board Setup does.
- Regenerate client bindings from this release's protos (new messages only; bindings
  generated from ipc.10 work against ipc.9).

**v10.0.6-ipc.9** — symbols can be placed the moment the editor opens; annotation and field placement over IPC.

- **Library on demand:** symbol libraries load in the background after the
  Schematic Editor opens (up to a minute on a busy machine). Until a library was
  in, `CreateItems` answered "library symbol not found" about a symbol that
  exists. It now makes sure that one library is loaded first: it waits while a
  background worker is parsing it, otherwise loads it, as symbol linking does when
  a schematic is opened. A real miss now says which it is: the library failed to
  load (with the reason), or the library has no such symbol.
- **`AnnotateSymbols`** (new, `schematic_commands.proto`): annotates as the
  Annotate Schematic dialog does, in one undoable commit, with scope, order,
  numbering, start number, reset and regroup options. The response lists every
  symbol instance whose reference changed, how many are still unannotated, and
  the errors KiCad's own annotation check reports.
- **Field placement:** `UpdateItems` accepts a `SchematicField` on its own and
  applies its position, visibility and text to that field of its symbol (text
  through the same setters as a whole-symbol update). Updating the whole symbol
  still moves its fields along and takes only their text.
- **Project of an open schematic:** `GetOpenDocuments` now fills `project.name`
  and `project.path` for schematics, as the board editor does, so clients can
  tell apart same-named schematics open in different KiCad instances.
- Regenerate client bindings from this release's protos (new messages only;
  bindings generated from ipc.9 work against ipc.8).

**v10.0.6-ipc.8** — symbols placed over IPC connect properly; junctions and global labels work.

- **Body style:** `CreateItems` passed the proto default body style 0 straight
  through to new symbols. Body styles are 1-based, and a symbol's connection points
  skip every pin whose body style differs from the symbol's. So a pin landing on a
  wire's mid-span never connected, and cleanup deleted the junction placed there.
  New symbols now default to body style 1 (out-of-range values return
  `ISC_INVALID_DATA`). `UpdateItems` keeps the current style when none is given and
  repairs a 0. The file reader reads `(body_style 0)` as 1, so schematics saved by
  ipc.2–ipc.7 are repaired on load and fixed on the next save.
- **Junctions:** `CreateItems` now places a junction the way the GUI does: on the
  bus layer when it sits on a bus, and splitting every wire that passes through it.
- **Global and hierarchical labels:** their (de)serializers were unimplemented
  stubs, so creating one failed with "could not unpack SCH_GLOBALLABEL". Both are
  implemented, with a new `shape` field (`LabelShape`: input, output,
  bidirectional, tri-state, passive). Regenerate client bindings from this release's
  protos.

**v10.0.6-ipc.7** — schematic `UpdateItems` edits symbols the way the Symbol Properties dialog does.

- Shared fields now stay in sync across the units of a multi-unit part
  (`SyncOtherUnits`, called by every interactive edit path). Before, setting
  U2's footprint over IPC changed only the one unit the update was sent for, so
  a 7-unit 74HC14 ended up with one footprinted unit and six empty ones.
- A Reference change is recorded in the sheet instance (`SetRef`). It used to
  change only the field text, which the saver ignores, so a rename over IPC was
  reverted by the next save.
- A unit change is recorded in the sheet instance (`SetUnitSelection`) and is
  range-checked (`ISC_INVALID_DATA`); previously it was also lost on save.
- Value changes go through `SetValueFieldText`, so design variants are respected.
- **Crash fix:** a schematic `CreateItems` or `UpdateItems` carrying two or more
  items (with no explicit `BeginCommit`) crashed eeschema. The handler pushed,
  and so freed, its commit after the first item, then used it for the next.
  It now pushes once after the whole request (one undo step), like pcbnew.

**v10.0.6-ipc.6** — schematic `CreateItems` keeps the requested unit.

- Symbols created over IPC were built with no sheet, so they had no per-sheet
  instance data. The saver writes a symbol's unit from that instance, so every
  multi-unit part placed through the API saved as unit 1 (and annotation saw
  them as unplaced instances). `CreateItems` now creates the symbol on the
  current sheet and records its reference and unit in the instance.
- A unit beyond the library symbol's unit count is rejected (`ISC_INVALID_DATA`)
  instead of producing an invalid symbol.

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
