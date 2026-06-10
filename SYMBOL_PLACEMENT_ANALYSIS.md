# KiCad IPC Symbol Placement — Root Cause Analysis

## Three Symbol Placement Paths

### Path 1: Interactive (Works)
`PlaceSymbol()` in `tools/sch_drawing_tools.cpp:131`

1. User picks symbol from chooser → `PICKED_SYMBOL` with `LibId`
2. `m_frame->GetLibSymbol(sel.LibId)` → `SchGetLibSymbol()` → `adapter->LoadSymbol()` → raw `LIB_SYMBOL*`
3. **Parametrized constructor**:
   ```cpp
   symbol = new SCH_SYMBOL(*libSymbol, &sheet, sel, cursorPos, &schematic);
   ```
   Inside constructor (`sch_symbol.cpp:82`):
   - `Init()` → creates 5 mandatory fields with proper FIELD_T types
   - `aSymbol.Flatten()` → standalone copy with inherited graphics
   - `part->SetParent()` → clears parent (IsRoot() = true)
   - `SetLibSymbol(part.release())` → stores in `m_part`, calls `UpdatePins()`
   - `UpdateFields(sheet, true, false, false, true, true)` → copies field text/style from lib

**Result**: `m_part` populated, fields correct, pins mapped. Renders perfectly.

### Path 2: File Loading (Works)
`SCH_SCREEN::UpdateSymbolLinks()` in `sch_screen.cpp:717`

1. Parser reads `lib_symbols` section → `SCH_SCREEN::m_libSymbols` map
2. Parser reads each symbol → SCH_SYMBOL with fields from S-expression (correct FIELD_T types)
3. Post-load resolution:
   - Check `m_libSymbols` cache first (embedded in file)
   - If found: `symbol->SetLibSymbol(new LIB_SYMBOL(*it->second))`
   - If not: `libs->LoadSymbol()` → `Flatten()` → `SetParent()` → add to cache → `SetLibSymbol()`
4. Fields already correct from parser

**Result**: `m_part` set from cache or library. Fields correct from file format.

### Path 3: IPC CreateItems (BROKEN)
`handleCreateUpdateItemsInternal()` in `api/api_handler_sch.cpp:170`

1. **Default constructor**: `make_unique<SCH_SYMBOL>()` → `Init()` creates 5 fields with correct FIELD_T. **`m_part = nullptr`**
2. **Deserialize** (`sch_symbol.cpp:3894`):
   ```cpp
   m_fields.clear();  // DESTROYS properly-typed fields
   for (const auto& f : symbol.fields()) {
       SCH_FIELD field(this, FIELD_T::USER);  // ALL become USER type
       field.SetName("Reference");  // Name set but type stays USER
       m_fields.push_back(field);
   }
   ```
   **`m_part` still nullptr** — Deserialize never touches it.
3. No library resolution in stock KiCad code. Our patch attempted post-hoc fixup but has issues.

## Key Data Structures

- **`m_part`** (`unique_ptr<LIB_SYMBOL>`): The flattened library symbol containing all draw items (rectangles, arcs, pins). This is what the renderer draws. If nullptr → renders "??" dummy.
- **`m_fields`** (`vector<SCH_FIELD>`): Symbol's fields (Reference, Value, etc.). Each has a `FIELD_T` type enum. Deserialize clobbers these to all `FIELD_T::USER`.
- **`m_libSymbols`** (`map<string, LIB_SYMBOL*>` on SCH_SCREEN): Cache of library symbols for file save/load. Keyed by `GetSchSymbolLibraryName()` which returns `m_lib_id.Format()`.
- **`m_pins`** (`vector<unique_ptr<SCH_PIN>>`): Symbol's pins, mapped to lib pins via `UpdatePins()`.

## Key Functions

- `LIB_SYMBOL::Flatten()` — Merges inherited graphics from parent chain. Returns standalone copy with `m_parent` reset.
- `LIB_SYMBOL::SetParent(nullptr)` — Clears `m_parent`. Makes `IsRoot()` return true.
- `SCH_SYMBOL::SetLibSymbol(LIB_SYMBOL*)` — Stores in `m_part`. Has guard: `wxCHECK2(!aLibSymbol || aLibSymbol->IsRoot(), aLibSymbol = nullptr)`. Calls `UpdatePins()`.
- `SCH_SYMBOL::UpdateFields()` — Iterates lib fields, matches to symbol fields by FIELD_T or name. Creates missing mandatory fields.
- `SCH_SYMBOL::UpdatePins()` — Maps symbol pins to lib pins by pin number.
- `SchGetLibSymbol()` (`sch_base_frame.cpp:79`) — Loads from adapter, falls back to legacy cache.
- `SCH_SCREEN::AddLibSymbol()` — Adds to screen's cache map for file save.

## The Correct Fix

Mirror the interactive path. Instead of default-construct → deserialize → post-hoc library fixup:

1. Deserialize proto to extract: `lib_id`, position, orientation, unit, field overrides
2. Load library symbol via `SchGetLibSymbol()` or `adapter->LoadSymbol()`
3. If found: construct SCH_SYMBOL using parametrized constructor `SCH_SYMBOL(LIB_SYMBOL&, ...)`
4. Apply proto field overrides (reference text, value text, custom fields) on top
5. Add to screen's `m_libSymbols` cache

This guarantees `m_part`, fields, and pins are all initialized correctly in one shot.

## Guard Rails

- `SetLibSymbol()` rejects non-root symbols (sets to nullptr). Always call `Flatten()` first.
- `GetField(FIELD_T)` auto-creates if missing — can lead to duplicate fields if old USER-typed ones linger.
- `UpdateFields()` with all-false flags still sets VALUE text from lib. Use carefully.

## Deployment

**CRITICAL: KiCad 10 loads `_eeschema.dll` (NOT `_eeschema.kiface`).**

The build outputs `_eeschema.dll` and so does the install. Never rename to `.kiface`.

```
Build output:  kicad-source\build\msvc-win64-release\eeschema\_eeschema.dll
Deploy to:     C:\Program Files\KiCad\10.0\bin\_eeschema.dll
```

Scripts:
- `build-and-deploy-eeschema.bat` — build + deploy in one step (run elevated)
- `deploy-eeschema.bat` — deploy only (run elevated)

Workflow from pi:
```bash
taskkill //IM eeschema.exe //F
cmd.exe //c "C:\\Users\\Robert\\Programs\\kicad-source\\build-and-deploy-eeschema.bat"
# Then start eeschema
```

## Wire Connectivity Rules

### T-junctions don't connect in kicad-cli ERC
A wire endpoint touching the MIDDLE of another wire (T-junction) is NOT recognized as a connection
by kicad-cli ERC. All connections must be endpoint-to-endpoint.

**Solution**: Segment horizontal/vertical rails at every junction point so each stub connects
at a rail endpoint (L-junction), not the middle.

### IPC wire coordinates have micro-offsets
IPC wires use `int(round(mm * 10000))` for coordinates. Due to floating-point math, these can differ
by ~0.01mm from the text representation that kicad-cli reads. This causes "pin not connected" ERC errors.

**Solution**: Use kicad_sch_api (file-based) for wire creation. It writes the same text representation
that kicad-cli reads, ensuring exact coordinate matching. Use IPC only for component placement.

### Labels must be at wire endpoints
Labels placed on wire middles or floating in space show "Label not connected" ERC errors.
Place labels at wire junction points (where two wire segments meet).

## File Locations
- `eeschema/sch_symbol.cpp` — constructors (L82), Init (L178), SetLibSymbol (L254), UpdatePins (L314), UpdateFields (L1371), Deserialize (L3894)
- `eeschema/tools/sch_drawing_tools.cpp` — PlaceSymbol (L131), constructor call (L431)
- `eeschema/sch_screen.cpp` — UpdateSymbolLinks (L717), AddLibSymbol (L1438)
- `eeschema/sch_painter.cpp` — draw symbol (L2707), GetDummy fallback (L2708)
- `eeschema/lib_symbol.cpp` — Flatten (L551), SetParent (L506), GetDummy (L370)
- `eeschema/api/api_handler_sch.cpp` — CreateItems handler (L170)
- `eeschema/api/api_sch_utils.cpp` — CreateItemForType (L43)
- `eeschema/sch_base_frame.cpp` — SchGetLibSymbol (L79), GetLibSymbol (L277)
