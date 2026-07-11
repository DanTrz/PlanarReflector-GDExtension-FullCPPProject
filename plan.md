# PlanarReflectorClippingCPP — Simple, Self-Contained Plan

## Context

A new node `PlanarReflectorClippingCPP` adds water-clipped planar reflections (compatible
shaders discard below-water fragments only in the reflection pass) alongside the untouched
legacy `PlanarReflectorCPP`. A working single-class implementation already exists in
`src/PlanarReflectorClippingCPP.cpp/.h` and follows the proven LocalDev prototype
(`PlanarReflector-CPP/LocalDev/planar_clip_prototype.gd`).

The previous `plan.md` was rejected: over-engineered (separate `PlanarReflectionClippingRuntime`
service class, `PlanarReflectionMath` files, 6-state lifecycle enum, ObjectID caching), ignored
legacy idioms, wrong editor-camera design, and still missed requirements.

**Governing principle (above all else): KEEP IT SIMPLE. One clean class, reusing the patterns
of the legacy `src/PlanarReflectorCPP.cpp`. No new service classes, no new source files, no
state machine.**

## Superseding shared-global decision

This section supersedes every ownership/authority/handoff reference later in this historical
plan. There is no clipping owner. Every enabled `PlanarReflectorClippingCPP` remains clipped.
Moving any reflector publishes its world Y as the shared height; changing any marker publishes
the shared marker. The most recent relevant modification wins. A static participant list exists
only for event-driven camera-mask refresh and diagnostics. Differing local values produce an
explanatory warning and never demote a node to an unclipped reflector.

## Approach

Refine the existing `PlanarReflectorClippingCPP` **in place**. Architecture stays exactly as
started:

- One class, `MeshInstance3D`-derived, same shape as the legacy class (`ENTER_TREE` →
  `call_deferred` setup → `_process` cadence loop → `_exit_tree` cleanup).
- **Fully event-driven initialization — no polling, no retry loop.** Setup runs exactly once
  per tree entry (deferred). `_process` is enabled ONLY after successful setup and while
  `is_active`; it contains no initialization checks. An uninitialized, disabled, or
  out-of-tree reflector does zero per-frame work.
- Editor camera via direct `EditorInterface::get_editor_viewport_3d(0)->get_camera_3d()`
  (the prototype's proven approach), resolved **once at setup** — never re-fetched per frame
  or per update (req 9.8). The viewport-0 camera is a persistent editor-UI node that lives
  for the whole editor session; a `tree_exiting` connection on it (read-only, no editor
  modification) re-resolves once in the rare case the editor rebuilds its viewports. The
  req-9.9 measurement step verifies this identity stability on Godot 4.6.
- Global ownership stays the simple static pointer `global_water_owner` + static
  `global_parameters_initialized` — no service class. Handoff is **event-driven and
  deterministic** (req 11.6, 11.7): a small static candidate list
  (`static Vector<PlanarReflectorClippingCPP *> ownership_candidates`) that clipping-enabled
  reflectors join at setup and leave at teardown/disable. `release_global_water_ownership()`
  (called from `_exit_tree`, `set_is_active(false)`, and the clipping-toggle setter) grants
  ownership to the first remaining candidate at that moment — registration order, no frame
  checks, no polling anywhere. The destructor also clears owner/list entries as a last-resort
  guard.
- Shader opt-in stays the existing 3-file package:
  `planar_reflection_clip.gdshaderinc` (already correct — global uniforms + one `discard`
  function), `planar_reflection_clip_example.gdshader`,
  `planar_reflection_clipping_surface.gdshader` (already has water_color / alpha /
  reflection_strength uniforms per req 8.1).

## Changes

### 1. Decouple from the legacy subsystem (req 1.3, 2.2, 18.3)

**`src/PlanarReflectorClippingCPP.cpp/.h`:**
- `_ready()`: remove `add_to_group("planar_reflectors")` (legacy group belongs to legacy class
  only). Keep own group `planar_reflectors_clipping`.
- Delete the plugin-facing compatibility API: `set_editor_camera()`, `get_active_camera()`,
  `is_planar_reflector_active()` public methods and their `_bind_methods` entries. The internal
  `get_source_camera()` is the single camera flow.

**`PlanarReflector-CPP/addons/PlanarReflectorCpp/plugin.gd`:**
- `_handles()`: return true only for `"PlanarReflectorCPP"`.
- Delete `ensure_clipping_shader_globals()` and the `WATER_HEIGHT_GLOBAL` /
  `CAMERA_BIT_GLOBAL` constants (the C++ class's `ensure_global_parameters()` already owns
  this; keep it there).
- Delete the icon-registration block in `_enter_tree()` — the `.gdextension` `[icons]`
  section (`bin/PlanarReflectorCpp.gdextension:18-20`) already registers both icons, so this
  code is redundant for both classes.
- Everything else (helper singleton, `_forward_3d_gui_input`, legacy camera distribution)
  stays byte-for-byte.

### 2. Event-driven initialization (req 12.3, 12.4, 12.9, 9.8)

**Initialization is 100% event-driven. `_process` plays NO role in initialization, camera
discovery, or ownership — it exists solely for the cadenced reflection render (req 12.5/12.6)
and is enabled only on a successfully initialized, active reflector.**

**DELETE from the started code (all three are polling and are replaced by events):**
1. The `if (!setup_complete) ensure_initialized()` retry at the top of `_process()`.
2. The 30-frame `editor_camera_refresh_countdown` editor-camera re-fetch in `_process()`
   (replaced by one-time resolution at setup + `tree_exiting` connection).
3. The per-frame `global_water_clipping_enabled && !global_water_owner` ownership re-acquire
   in `_process()` (replaced by the ownership candidate list — see Approach).

The event-driven design:

- **Events that trigger (re)initialization** — each calls the same single idempotent
  deferred setup, guarded by the existing `setup_generation` check (req 15.8):
  - `NOTIFICATION_ENTER_TREE` (covers `_ready`, scene open, tab switch back, reparenting —
    `_ready` only fires once, ENTER_TREE fires every time)
  - `set_main_camera()` — assigning a camera at runtime
  - `set_is_active(true)` — re-enabling
- **Everything setup needs already exists at deferred-ENTER_TREE time**: exported properties
  (mesh, material, `main_camera`) are assigned during scene instantiation before `_ready`;
  `get_world_3d()` is valid once inside the tree; the editor's 3D viewports are constructed
  before any edited scene loads (verified by the measurement step below).
- **If a prerequisite is genuinely missing** (e.g. no `main_camera` at runtime, no material):
  setup reports a configuration warning and stops. `set_process` stays **off**. The node does
  nothing until the next event (a setter call) — no waiting loop, no periodic checks.
- `set_process(true)` happens only on successful setup while `is_active`; `_process()` then
  contains ONLY the cadenced reflection work. One exception, one branch: if the surface
  material was missing at setup (MeshInstance3D offers no material-changed signal), the
  cadenced update includes `if (!bound_reflector_material.is_valid()) bind_reflection_texture();`
  — a single pointer check inside work that is already running, which becomes permanently
  false-cost once bound.

Keep the existing teardown order (already correct): clear texture params → `UPDATE_DISABLED`
→ remove child → `queue_free()` → null pointers; release ownership first in `_exit_tree()`.

### 3. Adopt legacy patterns where the new class diverges

- **Default environment** (`setup_environment()`): when `use_custom_environment` is off,
  instantiate the same default reflection Environment the legacy class builds
  (`BG_CLEAR_COLOR`, ambient color source, gray 0.8 ambient, energy 1.0 — see legacy
  `setup_reflection_environment()`, `src/PlanarReflectorCPP.cpp:312`) instead of assigning an
  empty `Ref<Environment>()`.
- **Shadow control** (req 13.5): set `positional_shadow_atlas_size` on the SubViewport like
  legacy does (2048), exposed as one exported int property `shadow_atlas_size`
  (enum hint: `Disabled:0,1024,2048,4096`) so expensive shadows can be turned off.

### 4. Change-only updates (req 12.7)

- `sync_projection()`: cache the last-applied source values (projection, fov, size, near,
  far, keep_aspect, h/v offset, frustum_offset) in one small struct member; compare and
  write camera properties only when a value changed. Mirrors the existing
  `last_viewport_size` pattern already used for viewport size.
- `configure_reflection_camera()`: only call `set_cull_mask` when the effective mask changed
  (cache `last_applied_cull_mask`).
- Everything else already complies: viewport size (`last_viewport_size`), water height
  (`last_published_height`), marker bit (published only from its setter / acquire),
  texture binding (only at init / material change), `UPDATE_ONCE` cadence gated by
  `update_frequency` modulo.

### 4b. Safe camera references (req 10.3, 10.4)

- `set_main_camera()`: besides assigning, when `setup_complete` re-run the sync path
  (projection / viewport size / transform + `request_reflection_render()`) so changing the
  camera takes effect immediately through the setter (req 10.3); when not set up, it is one
  of the initialization-triggering events (change 2).
- Freed-camera safety (req 10.4): connect to the assigned camera's `tree_exiting` signal in
  the setter (disconnect from the previous one); the handler nulls `main_camera` and disables
  processing with a configuration warning. Event-driven — no validity polling. Deleting or
  replacing the camera can never leave a dangling pointer.
- Editor camera safety works the same way: it is resolved once at setup into the
  `editor_camera` member, with a `tree_exiting` connection that clears it and re-resolves
  once if the editor ever rebuilds its viewports (see Approach). Delete
  `editor_camera_refresh_countdown` from the started code.

### 5. Diagnostics (req 16.x) — extend `_get_configuration_warnings()` only

Already covered: missing mesh, non-ShaderMaterial, missing runtime main_camera, second
ownership requester. Add:
- Marker layer conflict: warn when `get_marker_bit() & reflection_layers` is non-zero
  (objects on the marker layer would be reflected as scene content) (req 16.6).
- Global type conflict: when `ensure_global_parameters()` fails (existing type-mismatch
  branches), set a `bool global_parameter_conflict` flag and surface it as a warning plus a
  one-time `UtilityFunctions::push_warning` (req 16.4).
- One-time `push_warning` when a second reflector requests ownership (req 16.5), in
  `acquire_global_water_ownership()`'s conflict branch (guard with a bool so it doesn't spam).

No scans — all warnings derive from state the node already holds.

### 6. Editor lifecycle measurement (req 9.9) — do this FIRST during implementation

Two things get measured with temporary `UtilityFunctions::print` lines in
`NOTIFICATION_ENTER_TREE` / deferred setup / `_exit_tree` before the design is finalized:

**(a) Cold-start camera availability:** on a cold editor start that restores a reflector
scene, print whether `EditorInterface::get_editor_viewport_3d(0)->get_camera_3d()` is
non-null at deferred-ENTER_TREE time. Expected: yes (the editor UI is built before edited
scenes load). Only if this ever measures null, add a single one-shot
`SceneTree::create_timer` fallback (one deferred re-attempt, not a loop).

**(b) Tab-switch lifecycle:** in Godot 4.6 open two scene
tabs containing reflectors and switch between them. Expected (and the basis of this design):
inactive edited scenes leave the SceneTree, so `_exit_tree`/ENTER_TREE naturally stop/start
reflectors per tab (req 9.3–9.7) with zero extra code. Record the observed behavior in the
docs file (below), then remove the prints. If 4.6 does NOT remove inactive tabs from the
tree, fall back to checking `is_visible_in_tree()` in `_process` — still no scans.

### 7. Documentation (req 6.5)

Create `PlanarReflector-CPP/addons/PlanarReflectorCpp/SupportFiles/Shaders/PLANAR_REFLECTION_CLIPPING.md`:
- The 3-line opt-in recipe: `#include` the `.gdshaderinc` + call
  `planar_reflection_clip(CAMERA_VISIBLE_LAYERS, CAMERA_POSITION_WORLD, world_position)`
  in `fragment()`, with the `(INV_VIEW_MATRIX * vec4(VERTEX,1.0)).xyz` world-position snippet.
- Copy-paste examples for PBR, toon, and emission shaders (the existing
  `planar_reflection_clip_example.gdshader` is the base).
- Notes: unsupported materials keep full traditional reflections; one clipping owner per
  project; marker layer setup; give each reflector its own surface-material instance
  (or `resource_local_to_scene`) so instances don't overwrite each other (req 8.4).

### 8. No changes needed

- `src/PlanarReflectorCPP.cpp/.h` — untouched (req 1.1).
- `src/register_types.cpp` — already registers both classes; fine as is.
- `planar_reflector_editor_helper.gd` — untouched.
- `planar_reflection_clip.gdshaderinc` — already matches req 5.1/5.2/5.4 exactly.
- `bin/PlanarReflectorCpp.gdextension` — already has both icons.

## Requirements traceability matrix

Every numbered item in `requirements.md` mapped to its mechanism. (Old `plan.md` is rejected
and is not an input to this plan.)

| Req | Mechanism |
|---|---|
| 1.1–1.2 | `PlanarReflectorCPP.h/.cpp` untouched; legacy scenes/plugin path unchanged |
| 1.3 | plugin.gd stripped to legacy-only (change 1) |
| 1.4 | `register_types.cpp` already registers both (no change) |
| 2.1–2.3 | Existing single class; owns its SubViewport, camera, binding, lifecycle |
| 2.4 | Runtime children created without an owner → never serialized |
| 2.5 | Init from ENTER_TREE event, never from selection/clicks (change 2) |
| 3.1, 3.6 | Main camera cull mask never touched; it never carries the marker bit → shader never discards for it |
| 3.2–3.4 | `.gdshaderinc`: discard only when marker bit present AND camera below water AND fragment below water |
| 3.5 | Only the reflection camera's cull mask gets the marker bit |
| 3.7, 9.10 | Editor camera is read-only (transform/projection reads; one signal connection) |
| 3.8–3.9 | `sync_projection` + mirror math in `sync_reflection_transform` (prototype-proven) |
| 4.1–4.4 | One horizontal height; owner's `global_position.y`; tilted/multi-height not implemented |
| 4.5, 11.2–11.3 | Single static `global_water_owner`; `acquire` refuses when another owner exists |
| 5.1–5.2 | `ensure_global_parameters()` creates exactly `float planar_water_height` / `uint planar_reflection_camera_bit` |
| 5.3 | Exported `reflection_camera_marker_layer` (1–20) |
| 5.4 | `PLANAR_REFLECTION_CLIP_BIAS` const in the include |
| 5.5 | RenderingServer globals only — no per-material writes for clipping |
| 5.6 | Height published from `NOTIFICATION_TRANSFORM_CHANGED` + `last_published_height` guard |
| 5.7 | Marker bit published only from its setter / ownership acquire |
| 5.8 | Static `global_parameters_initialized` — validation runs once, never per frame |
| 6.1–6.4 | Opt-in `#include` + one function call; artist shading untouched |
| 6.5 | New `PLANAR_REFLECTION_CLIPPING.md` with PBR/toon/emission examples (change 7) |
| 6.6–6.7 | No code path reads, rewrites, scans, or duplicates user materials |
| 7.1–7.5 | Unsupported materials are never referenced by the system → unchanged, fully visible, traditional full reflection |
| 8.1–8.2 | Surface shader uniforms: `water_color`, alpha, `reflection_strength`; alpha blend keeps underwater visible |
| 8.3 | `bind_reflection_texture()` binds this instance's own ViewportTexture |
| 8.4 | Each instance binds only its own material; doc note: unique material per reflector |
| 9.1–9.2 | Deferred ENTER_TREE setup; no selection/click/reload (change 2) |
| 9.3–9.7 | Editor detaches inactive tab scenes from the SceneTree → EXIT_TREE tears down, ENTER_TREE re-initializes (verified by 9.9 step) |
| 9.8 | Editor camera resolved once at setup; no per-frame searches, no SceneTree scans (audit fix 1) |
| 9.9 | Measurement step runs FIRST (change 6): cold-start camera availability + tab-switch lifecycle prints on 4.6 |
| 10.1–10.2 | Runtime camera = exported `main_camera` only; zero scene scans |
| 10.3 | `set_main_camera()` re-syncs immediately (change 4b) |
| 10.4 | `tree_exiting` connection nulls the reference — event-driven, no validity polling (change 4b) |
| 10.5 | `_exit_tree` full teardown |
| 10.6 | `EditorInterface` only behind `is_editor_hint()`; no editor deps in exports |
| 11.1 | Per-instance SubViewport/camera/binding |
| 11.4, 16.5 | Config warning + one-time `push_warning` on second ownership request |
| 11.5 | Non-owners: no marker bit in mask, no global writes — plain reflectors |
| 11.6–11.7 | Static candidate list; release (disable/exit/delete) grants to first registered candidate — deterministic, event-driven (audit fix 2) |
| 12.1–12.2 | No object/material/camera scans; globals validated once |
| 12.3 | No retry loop — setup runs on events only, stops existing entirely after success (change 2) |
| 12.4 | Failed-setup/disabled/detached reflectors: `set_process(false)` + `UPDATE_DISABLED` |
| 12.5–12.6 | `frame_counter % update_frequency` gates sync AND render; `UPDATE_ONCE` per render, never `UPDATE_ALWAYS` |
| 12.7 | Dirty caches: projection struct, `last_viewport_size`, cull mask, texture binding, `last_published_height` (change 4) |
| 12.8 | Per-node O(1) work; no shared scans to multiply |
| 12.9 | No reflectors → no statics ticking, no processing — zero cost |
| 13.1–13.2 | `resolution_mode` (Match/Scaled/Fixed) + `resolution_scale` |
| 13.3 | `use_lod` + near/far + multiplier |
| 13.4 | 128×128 minimum clamp in `calculate_target_size` |
| 13.5 | `shadow_atlas_size` property (Disabled/1024/2048/4096) (change 3) |
| 14.1–14.11 | All preserved as exported properties (started code already has them; editor preview inherent; texture injection = `bind_reflection_texture`) |
| 15.1–15.2 | Generation-guarded idempotent setup; `create_runtime_nodes()` early-outs if nodes exist |
| 15.3–15.4 | Teardown order: clear texture refs → `UPDATE_DISABLED` → free |
| 15.5–15.7 | Ownership released in `_exit_tree`; refs nulled; `queue_free` exactly once |
| 15.8 | `setup_generation` check rejects stale deferred calls |
| 15.9 | Reload works (ENTER_TREE re-init) but is never required |
| 15.10 | Teardown clears ViewportTexture refs; `.gdextension` `reloadable = true` |
| 16.1–16.6 | `_get_configuration_warnings()`: mesh, material, runtime camera, global type conflict, ownership conflict, marker∩reflection_layers (change 5) |
| 16.7 | All warnings derive from state the node already holds — no scans |
| 17.1–17.13 | Verification section below, item-for-item |
| 18.1–18.5 | One class owns everything; single setup path; single camera/ownership/teardown flows; no plugin (no lifecycle evidence requires one); deterministic + warning diagnostics |

## Files touched

| File | Action |
|---|---|
| `src/PlanarReflectorClippingCPP.h/.cpp` | Refine in place (changes 1–5) |
| `PlanarReflector-CPP/addons/PlanarReflectorCpp/plugin.gd` | Legacy-only cleanup (change 1) |
| `.../SupportFiles/Shaders/PLANAR_REFLECTION_CLIPPING.md` | New doc (change 7) |
| `plan.md` (repo root) | Superseded — delete or replace with this plan |

## Build

`scons` from the repo root (existing SConstruct; Windows `template_debug` x86_64 as per the
current `.obj` artifacts). Output DLL already wired in the `.gdextension`.

## Verification (maps to req 17)

1. **Measure first** (change 6): tab-switch print test in Godot 4.6 — confirms the
   enter/exit-tree trigger mechanism before finalizing.
2. **Cold editor start**: close Godot, reopen, open a scene with the clipping reflector —
   reflection appears and tracks the editor camera with no selection, click, or reload.
3. **Tab switching**: two scenes with reflectors; switch tabs — active scene reflects,
   previous scene's reflector stops (verify via Godot's debugger monitor: viewport draw calls).
4. **Cameras**: toggle editor + runtime cameras between Perspective/Orthogonal; reflection
   position/orientation/scale stay correct (prototype scene is the reference).
5. **Clipping behavior**: compatible object above / crossing / below water → full / partial /
   no reflection; StandardMaterial3D object stays fully reflected and fully visible to the
   main camera; main camera never clips.
6. **Ownership**: two clipping reflectors with `global_water_clipping_enabled` — second gets
   the config warning + one push_warning; delete the owner → survivor acquires ownership and
   clipping continues.
7. **Lifecycle**: enable/disable `is_active` (viewport stops updating), delete, reparent
   (re-entry re-initializes), reload scene, run exported/game mode with `main_camera` set.
   Also free/replace the assigned `main_camera` at runtime — no crash, no errors, and the
   reflector resumes when a new camera is assigned via the setter.
8. **Performance**: with no clipping reflector in the scene, profiler shows zero new-class
   work; a reflector that failed setup (e.g. no runtime camera) or is disabled has
   `is_processing() == false` — zero per-frame work; steady state shows no per-frame property
   writes (spot-check with a breakpoint or temporary counter in `sync_projection`'s write path).
9. **Legacy regression**: open the existing legacy demo scene — `PlanarReflectorCPP` behaves
   exactly as before; plugin/helper still drive it.
