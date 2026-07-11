# PlanarReflectorClippingCPP Requirements

## 1. Existing-system compatibility

1.1. `PlanarReflectorCPP` must remain unchanged.
1.2. Existing scenes using `PlanarReflectorCPP` must continue working.
1.3. The existing editor plugin and editor helper must serve the legacy class only.
1.4. Both reflector classes must be available from the same GDExtension binary.

## 2. New node and ownership

2.1. Create a custom node named `PlanarReflectorClippingCPP`.
2.2. It must be independent of `PlanarReflectorCPP`.
2.3. Each instance must own its reflection camera, `SubViewport`, texture binding, initialization, updates, and cleanup.
2.4. Generated runtime nodes must not be serialized into the scene.
2.5. Initialization must not depend on selecting or clicking the node.

## 3. Reflection behaviour

3.1. The main camera must always render complete objects, including geometry below transparent water.
3.2. For compatible objects, a fully above-water object must produce a complete reflection.
3.3. For compatible objects, an object crossing the water must reflect only its above-water portion.
3.4. For compatible objects, a fully underwater object must produce no reflection.
3.5. Clipping must affect only rendering from the reflection camera.
3.6. The main game camera must never receive reflection clipping.
3.7. The editor camera must never be permanently modified.
3.8. Perspective and Orthogonal cameras must both be supported.
3.9. Reflections must have the correct position, orientation, and scale.

## 4. Initial water model

4.1. The initial implementation supports one horizontal global clipping-water level.
4.2. The clipping height comes from the authoritative reflector's world Y position.
4.3. Tilted clipping planes are out of scope for the initial implementation.
4.4. Simultaneous clipping at several water heights is out of scope.
4.5. Several clipping reflector nodes may exist and all must use the same shared global clipping values.

## 5. Global shader parameters

5.1. Water height must be shared as `global uniform float planar_water_height`.
5.2. The reflection-camera marker must be shared as `global uniform uint planar_reflection_camera_bit`.
5.3. The marker bit must be configurable per project.
5.4. The clipping bias may be hardcoded in the shader include.
5.5. Global values must not be updated per material.
5.6. The shared manager publishes the world Y of the closest enabled reflector whose world-space mesh bounds intersect the active source camera frustum.
5.7. Changing any enabled clipping reflector's marker publishes it as the shared marker; the last modified setting wins.
5.8. Global-uniform definitions and types must not be repeatedly validated during normal frames.

## 6. Compatible shader materials

6.1. Clipping support is opt-in for custom spatial shaders.
6.2. Artists must receive a reusable `.gdshaderinc` requiring minimal integration changes.
6.3. The include must work with PBR, toon, emission, and special-effect shaders.
6.4. It must not replace or interfere with the artist's existing shading logic beyond the explicit clipping call.
6.5. The addon must provide documented integration examples.
6.6. The addon must not rewrite shader source automatically.
6.7. The addon must not scan, modify, or duplicate arbitrary user materials.

## 7. Unsupported materials

7.1. Materials without clipping support must continue working unchanged.
7.2. They must remain completely visible to the main camera.
7.3. They must continue producing their traditional, unclipped full reflection.
7.4. `StandardMaterial3D` must not be replaced or broken.
7.5. Unsupported materials must not cause errors or missing main-view geometry.

## 8. Reflector surface

8.1. The surface must expose configurable water colour, transparency, and reflection strength.
8.2. It must allow underwater geometry to remain visible.
8.3. Its material must receive the correct reflection `ViewportTexture`.
8.4. Separate reflector instances must not overwrite one another's presentation materials.

## 9. Editor operation

9.1. Opening a scene containing `PlanarReflectorClippingCPP` must show reflections automatically.
9.2. Initialization must require no node selection, viewport click, or scene reload.
9.3. Switching scene tabs must activate reflectors belonging to the newly active scene.
9.4. Reflectors from the previously active scene must stop rendering.
9.5. Closing a scene must release that scene's reflection resources.
9.6. Multiple clipping reflectors in one active scene must receive the correct editor camera.
9.7. Inactive editor scenes must not continue reflection rendering.
9.8. The final integration must not continuously search for cameras or scan the `SceneTree` per frame.
9.9. Godot 4.6 editor lifecycle behaviour must be measured before selecting the final event/trigger mechanism.
9.10. Editor integration must be scoped to relevant scenes and must not permanently alter editor-camera settings.

## 10. Runtime operation

10.1. The runtime source camera must come from the exported `main_camera` property.
10.2. The system must not scan the `SceneTree` to find a runtime camera.
10.3. Changing `main_camera` must update the reflector through its setter.
10.4. Removing or replacing the camera must not leave invalid references.
10.5. Runtime scene changes must cleanly destroy old reflection resources.
10.6. Exported projects must work without editor-only dependencies.

## 11. Multiple reflectors and shared globals

11.1. Every reflector must own a separate `SubViewport`, reflection camera, and texture binding.
11.2. Every enabled clipping reflector must receive the shared camera marker and remain clipped.
11.3. Height selection is resolved centrally from registered, valid, in-tree reflector ObjectIDs; no reflector owns the global value.
11.4. The shared height uses nearest-in-frustum selection with hysteresis; the shared marker retains last-modified semantics.
11.5. Reflectors at a different Y from the selected shared height must remain clipped and receive a clear explanatory warning.
11.6. Reflectors whose local marker setting differs from the current shared marker must remain clipped and receive a clear explanatory warning.
11.7. Disabling, removing, or queue-freeing the selected reflector must safely trigger reselection without disabling remaining reflectors.

## 12. Performance requirements

12.1. Do not scan scene objects, materials, or cameras every frame.
12.2. Do not validate global-uniform definitions every frame.
12.3. Initialization checks must stop after successful initialization.
12.4. Disabled and inactive-scene reflectors must stop rendering.
12.5. `update_frequency` must control actual reflection renders, not only camera synchronization.
12.6. A lower update frequency must use `SubViewport.UPDATE_ONCE`; the viewport must not remain `UPDATE_ALWAYS`.
12.7. Viewport size, projection state, reflection texture bindings, and global parameters must update only when changed.
12.8. Administrative CPU cost must be O(active reflectors), with constant work per active instance and no multiplied global scans.
12.9. No new-reflector processing must occur when no `PlanarReflectorClippingCPP` exists.

## 13. Resolution and quality

13.1. Support source-viewport-matched, scaled, and fixed reflection resolutions.
13.2. Support a configurable resolution scale.
13.3. Support distance-based reflection-resolution LOD.
13.4. Prevent invalid or zero-sized reflection textures.
13.5. Allow expensive reflection features, including shadows where applicable, to be controlled.

## 14. Exported features to preserve

14.1. Active toggle.
14.2. Main-camera selection.
14.3. Reflection layer mask.
14.4. Perspective/Orthogonal matching and Orthogonal scale control.
14.5. Custom environment assignment.
14.6. Reflection position and rotation offsets.
14.7. Offset scale and blend mode.
14.8. Update frequency.
14.9. Resolution controls and distance LOD.
14.10. Editor preview.
14.11. Reflection texture injection.

## 15. Lifecycle and cleanup

15.1. Initialization must be idempotent.
15.2. Runtime nodes must be created once per reflector instance.
15.3. Rendering must be disabled before teardown.
15.4. Texture references must be cleared before destroying the `SubViewport`.
15.5. The reflector must unregister from shared clipping diagnostics during teardown.
15.6. Camera and world references must be cleared safely.
15.7. Runtime nodes must be freed exactly once.
15.8. Deferred callbacks must not operate on exited or destroyed nodes.
15.9. Scene reloads must be supported but never required for initialization.
15.10. GDExtension reload must not leave dangling `ViewportTexture` or RID references.

## 16. Diagnostics

16.1. Warn when the reflector mesh is missing.
16.2. Warn when the reflector material is missing or incompatible.
16.3. Warn when the runtime main camera is missing.
16.4. Warn when global shader parameters have conflicting types.
16.5. Warn when reflector heights differ from the camera-selected shared height or marker settings differ from the shared marker.
16.6. Warn when the configured marker layer is invalid or conflicts with documented project use.
16.7. Warnings must not depend on expensive continuous scans.

## 17. Verification and acceptance

17.1. Test a cold Godot editor start and open a clipping-reflector scene without selecting anything.
17.2. Confirm the reflection appears without clicking the viewport or reloading the scene.
17.3. Test switching between multiple open scenes containing zero, one, and several reflectors.
17.4. Test Perspective and Orthogonal cameras in both the editor and runtime.
17.5. Test compatible objects fully above, crossing, and fully below water.
17.6. Test unsupported objects fully above, crossing, and fully below water.
17.7. Test overlapping compatible and unsupported objects.
17.8. Test enabling, disabling, deleting, and reparenting reflectors.
17.9. Test nearest-in-frustum height selection, hysteresis, marker updates, and deletion/queue-free of registered reflectors.
17.10. Test scene reloads and runtime scene transitions.
17.11. Measure CPU and GPU performance, including the no-reflector and inactive-reflector cases.
17.12. Confirm no stale camera, world, texture, RID, or viewport survives scene changes.
17.13. Confirm legacy `PlanarReflectorCPP` behaviour remains unchanged.

## 18. Architectural quality constraints

18.1. Legacy and clipping reflector responsibilities must have clear, documented ownership.
18.2. The new implementation must not rely on patchwork duplicate initialization paths.
18.3. Camera acquisition, reflection rendering, shared-global publication, and teardown must each have a single authoritative flow.
18.4. Any editor plugin or helper must exist only where verified Godot lifecycle evidence demonstrates it is required.
18.5. The final design must prioritize deterministic behaviour, separation of concerns, scalability, and observable failure diagnostics.
