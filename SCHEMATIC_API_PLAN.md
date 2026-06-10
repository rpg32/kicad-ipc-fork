# KiCad Schematic IPC API — Implementation Plan

## Current State

**Already working (with our constructor fix):**
- BeginCommit / EndCommit — transaction system ✅
- GetOpenDocuments — document queries ✅
- CreateItems/UpdateItems infrastructure — handler dispatches correctly ✅

**Already has Serialize/Deserialize (5 types):**
- SCH_LINE (wires/buses/graphic lines)
- SCH_LABEL (local net labels)
- SCH_GLOBALLABEL (global net labels)
- SCH_HIERLABEL (hierarchical labels)
- SCH_DIRECTIVE_LABEL (directive labels)

**Missing Serialize/Deserialize (13 types):**
- SCH_SYMBOL, SCH_SHEET, SCH_FIELD, SCH_PIN, SCH_SHEET_PIN
- SCH_JUNCTION, SCH_NO_CONNECT, SCH_BUS_ENTRY
- SCH_SHAPE, SCH_TEXTBOX, SCH_TEXT, SCH_BITMAP, SCH_TABLE

**Missing type mapping:** 0 entries in TypeNameFromAny for schematic types

**Missing handler registrations:** GetItems, RunAction, Selection, Save, etc.

---

## Implementation Phases

### Phase 1 — MVP: Wire + Label + Type Mapping (BUILD & TEST)
**Goal:** Draw wires and place labels live in eeschema via IPC
**Estimated: ~50 lines of C++ changes + rebuild**

Files to modify:
1. `common/api/api_utils.cpp` — Add type mappings for the 5 types that already have serialization:
   ```cpp
   { "type.googleapis.com/kiapi.schematic.types.Line", SCH_LINE_T },
   { "type.googleapis.com/kiapi.schematic.types.LocalLabel", SCH_LABEL_T },
   { "type.googleapis.com/kiapi.schematic.types.GlobalLabel", SCH_GLOBAL_LABEL_T },
   { "type.googleapis.com/kiapi.schematic.types.HierarchicalLabel", SCH_HIER_LABEL_T },
   { "type.googleapis.com/kiapi.schematic.types.DirectiveLabel", SCH_DIRECTIVE_LABEL_T },
   ```

**Test:** Create a wire via IPC → should appear instantly in eeschema

### Phase 2 — Text + Junction + No-Connect
**Goal:** Place text annotations, junctions, and no-connect markers
**Estimated: ~300 lines of C++**

Files to modify:
1. `api/proto/schematic/schematic_types.proto` — Add Junction, NoConnect message types (~20 lines)
2. `eeschema/sch_text.cpp` — Add Serialize/Deserialize (~60 lines)
3. `eeschema/sch_junction.cpp` — Add Serialize/Deserialize (~40 lines)
4. `eeschema/sch_no_connect.cpp` — Add Serialize/Deserialize (~40 lines)
5. `common/api/api_utils.cpp` — Add type mappings (~3 lines)
6. `api/CMakeLists.txt` — Ensure new protos compiled

### Phase 3 — SchematicSymbol (THE BIG ONE)
**Goal:** Place components (resistors, caps, ICs) via IPC
**Estimated: ~800 lines of C++**

This is the most complex piece — a symbol has a library reference, position, rotation,
fields (Reference, Value, Footprint), pins, and unit info.

Files to modify:
1. `api/proto/schematic/schematic_types.proto` — Add SchematicSymbol message (~80 lines):
   ```protobuf
   message SchematicSymbol {
     common.types.KIID id = 1;
     common.types.LibraryIdentifier library_identifier = 2;
     common.types.Vector2 position = 3;
     common.types.Angle orientation = 4;
     bool mirror_x = 5;
     bool mirror_y = 6;
     int32 unit = 7;
     int32 body_style = 8;
     repeated SchematicField fields = 9;
     bool in_bom = 10;
     bool on_board = 11;
     bool dnp = 12;
   }
   
   message SchematicField {
     common.types.KIID id = 1;
     string name = 2;
     common.types.Text value = 3;
     bool visible = 4;
     common.types.Vector2 position = 5;
   }
   ```
2. `eeschema/sch_symbol.cpp` — Add Serialize/Deserialize (~200 lines)
3. `eeschema/sch_field.cpp` — Add Serialize/Deserialize (~80 lines)
4. `common/api/api_utils.cpp` — Add type mappings (~2 lines)
5. `eeschema/api/api_sch_utils.cpp` — Helper functions for symbol conversion (~100 lines)

**Test:** Place a resistor via IPC → should appear instantly with correct symbol and value

### Phase 4 — GetItems + Selection + RunAction
**Goal:** Read back schematic state, select items, trigger editor actions
**Estimated: ~400 lines of C++**

Files to modify:
1. `eeschema/api/api_handler_sch.cpp` — Register and implement:
   - `handleGetItems` — walk schematic tree, serialize each item (~100 lines)
   - `handleGetSelection` / `handleAddToSelection` / `handleClearSelection` (~80 lines)
   - `handleRunAction` — dispatch TOOL_ACTION by name (~40 lines)
   - `handleSaveDocument` / `handleRevertDocument` (~30 lines)
2. `eeschema/api/api_handler_sch.h` — Declare handler methods (~20 lines)

**Test:** GetItems returns all symbols/wires. RunAction("common.Control.zoomFitScreen") works.

### Phase 5 — Sheets + Bus + Shapes
**Goal:** Multi-sheet designs, bus architecture, graphic annotations
**Estimated: ~600 lines of C++**

1. `api/proto/schematic/schematic_types.proto` — Add Sheet, SheetPin, BusEntry, Shape (~60 lines)
2. `eeschema/sch_sheet.cpp` — Serialize/Deserialize (~120 lines)
3. `eeschema/sch_sheet_pin.cpp` — Serialize/Deserialize (~60 lines)
4. `eeschema/sch_bus_entry.cpp` — Serialize/Deserialize (~50 lines)
5. `eeschema/sch_shape.cpp` — Serialize/Deserialize (~60 lines)
6. `eeschema/sch_textbox.cpp` — Serialize/Deserialize (~60 lines)
7. `common/api/api_utils.cpp` — Add type mappings (~5 lines)

### Phase 6 — UpdateItems + DeleteItems + Properties
**Goal:** Modify existing items, change properties, delete items
**Estimated: ~400 lines of C++**

1. `eeschema/api/api_handler_sch.cpp`:
   - Implement `deleteItemsInternal` (currently TODO) (~60 lines)
   - Implement `getItemFromDocument` (currently TODO) (~40 lines)
   - Handle symbol property updates in `handleCreateUpdateItemsInternal` (~100 lines)
2. `eeschema/api/api_handler_sch.h` — Additional methods
3. Schematic-specific commands:
   - `handleGetTitleBlockInfo` (~30 lines)
   - `handleExpandTextVariables` (~20 lines)

### Phase 7 — Schematic-Specific Commands
**Goal:** Net queries, ERC, S-expression I/O, hierarchy navigation
**Estimated: ~600 lines of C++**

1. `api/proto/schematic/schematic_commands.proto` — Add schematic-specific commands:
   - GetNets, GetConnectedPins, GetSheetHierarchy
   - SaveDocumentToString, ParseAndCreateItemsFromString
2. `eeschema/api/api_handler_sch.cpp` — Implement handlers
3. `eeschema/api/api_handler_sch.h` — Declare handlers

---

## Build & Test Cycle

Each phase follows this cycle:
1. Make code changes
2. `cmake --build build/msvc-win64-release` (incremental, ~2-5 min after first build)
3. Run patched eeschema from build dir
4. Test via Python IPC client
5. If working, update pi extension tools to use IPC

## Total Estimates

| Phase | Lines | Time | Cumulative Capability |
|-------|-------|------|----------------------|
| 1 - Wire + Label | ~50 | 1 hour | Draw wires and labels live |
| 2 - Text + Junction | ~300 | 4 hours | Basic schematic annotations |
| 3 - Symbol | ~800 | 1-2 days | Place components live! |
| 4 - GetItems + Actions | ~400 | 1 day | Read back state, trigger actions |
| 5 - Sheets + Shapes | ~600 | 1 day | Multi-page, graphics |
| 6 - Update + Delete | ~400 | 1 day | Modify/remove existing items |
| 7 - Commands | ~600 | 1-2 days | Net queries, ERC, hierarchy |
| **Total** | **~3150** | **~6-9 days** | **Full schematic API** |

Note: This is less than the initial 5000-line estimate because we discovered that 5 types
already have Serialize/Deserialize, and the handler infrastructure (commits, document 
validation) already works thanks to our constructor fix.
