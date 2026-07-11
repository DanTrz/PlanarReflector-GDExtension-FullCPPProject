#include "PlanarReflectorClippingCPP.h"

#include <godot_cpp/classes/camera_attributes.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

ObjectID PlanarReflectorClippingCPP::last_height_writer_id;
ObjectID PlanarReflectorClippingCPP::last_marker_writer_id;
bool PlanarReflectorClippingCPP::global_parameters_initialized = false;
Vector<ObjectID> PlanarReflectorClippingCPP::clipping_participants;
double PlanarReflectorClippingCPP::shared_water_height = 0.0;
uint32_t PlanarReflectorClippingCPP::shared_marker_bit = uint32_t(1) << 19;
bool PlanarReflectorClippingCPP::shared_height_initialized = false;
bool PlanarReflectorClippingCPP::shared_marker_initialized = false;
ObjectID PlanarReflectorClippingCPP::selected_reflector_id;
uint64_t PlanarReflectorClippingCPP::last_selection_frame = uint64_t(-1);
bool PlanarReflectorClippingCPP::selection_dirty = true;
bool PlanarReflectorClippingCPP::selection_camera_cache_valid = false;
ObjectID PlanarReflectorClippingCPP::selection_camera_id;
Transform3D PlanarReflectorClippingCPP::selection_camera_transform;
int PlanarReflectorClippingCPP::selection_camera_projection = -1;
double PlanarReflectorClippingCPP::selection_camera_fov = 0.0;
double PlanarReflectorClippingCPP::selection_camera_size = 0.0;
double PlanarReflectorClippingCPP::selection_camera_near = 0.0;
double PlanarReflectorClippingCPP::selection_camera_far = 0.0;
double PlanarReflectorClippingCPP::selection_camera_h_offset = 0.0;
double PlanarReflectorClippingCPP::selection_camera_v_offset = 0.0;
Vector2 PlanarReflectorClippingCPP::selection_camera_frustum_offset;
int PlanarReflectorClippingCPP::selection_camera_keep_aspect = -1;
uint32_t PlanarReflectorClippingCPP::selection_camera_cull_mask = 0;
Vector2i PlanarReflectorClippingCPP::selection_camera_viewport_size;

PlanarReflectorClippingCPP::PlanarReflectorClippingCPP() {
    // Processing is enabled only after a successful setup (apply_activity_state).
    set_process(false);
    set_notify_transform(true);
}

PlanarReflectorClippingCPP::~PlanarReflectorClippingCPP() {
    const ObjectID id(get_instance_id());
    clipping_participants.erase(id);
    if (last_height_writer_id == id) {
        last_height_writer_id = ObjectID();
    }
    if (last_marker_writer_id == id) {
        last_marker_writer_id = ObjectID();
    }
    if (selected_reflector_id == id) {
        selected_reflector_id = ObjectID();
    }
    selection_dirty = true;
}

// ---------------------------------------------------------------------------
// Lifecycle: everything initializes from events (ENTER_TREE, setters).
// _process never initializes or discovers cameras.
// ---------------------------------------------------------------------------

void PlanarReflectorClippingCPP::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE: {
            add_to_group("planar_reflectors_clipping");
            // TEMP (req 9.9 measurement): remove after Godot 4.6 lifecycle verification.
            if (Engine::get_singleton()->is_editor_hint()) {
                UtilityFunctions::print("[PRClip TEMP] ENTER_TREE: ", get_name());
            }
            schedule_setup();
        } break;

        case NOTIFICATION_TRANSFORM_CHANGED: {
            if (!setup_complete) {
                break;
            }
            if (global_water_clipping_enabled) {
                selection_dirty = true;
            }
            Camera3D *source = get_source_camera();
            if (source) {
                sync_reflection_transform(source);
                request_reflection_render();
                if (debug_selection_diagnostics) {
                    UtilityFunctions::print("[PRClip Transform] reflector=", get_name(),
                            " plane_y=", get_global_position().y,
                            " selected=", ObjectID(get_instance_id()) == selected_reflector_id,
                            " reflection_camera_y=", reflection_camera ? reflection_camera->get_global_position().y : 0.0,
                            " effective_mask=", int64_t(get_effective_reflection_mask()));
                }
            }
        } break;

        case NOTIFICATION_VISIBILITY_CHANGED: {
            selection_dirty = true;
        } break;

        case NOTIFICATION_EDITOR_PRE_SAVE: {
            if (Engine::get_singleton()->is_editor_hint() && bound_reflector_material.is_valid()) {
                // ViewportTexture stores a NodePath to the generated SubViewport. It is
                // valid only for this live editor instance and must never be serialized.
                editor_texture_binding_suspended = true;
                bound_reflector_material->set_shader_parameter("reflection_screen_texture", Variant());
                bound_reflector_material->set_shader_parameter("reflection_texture", Variant());
                bound_reflector_material->set_shader_parameter("debug_selection_highlight", 0.0);
            }
        } break;

        case NOTIFICATION_EDITOR_POST_SAVE: {
            if (Engine::get_singleton()->is_editor_hint() && editor_texture_binding_suspended) {
                call_deferred("restore_reflection_texture_after_save");
            }
        } break;
    }
}

void PlanarReflectorClippingCPP::schedule_setup() {
    setup_generation++;
    setup_complete = false;
    call_deferred("deferred_setup", setup_generation);
}

void PlanarReflectorClippingCPP::deferred_setup(int p_generation) {
    if (p_generation != setup_generation || !is_inside_tree()) {
        return;
    }
    setup_complete = ensure_initialized();
    // TEMP (req 9.9 measurement): remove after Godot 4.6 lifecycle verification.
    if (Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("[PRClip TEMP] deferred_setup: ", get_name(),
                " ok=", setup_complete, " editor_camera=", editor_camera != nullptr);
    }
    if (!setup_complete) {
        // A prerequisite is missing: stay inert until the next event (a setter call
        // or tree re-entry). No retries, no polling.
        set_process(false);
        update_configuration_warnings();
    }
}

bool PlanarReflectorClippingCPP::ensure_initialized() {
    if (!is_inside_tree() || !is_active) {
        return false;
    }
    // Safety net for the module-init path (RenderingServer may not exist that early,
    // as measured at game startup): by node-setup time every singleton is alive, so
    // this guarantees the shader globals exist and are persisted to Project Settings
    // without any user action. Flag-guarded - effectively free after the first call.
    ensure_global_shader_parameters();
    create_runtime_nodes();
    if (!reflection_viewport || !reflection_camera) {
        return false;
    }

    Ref<World3D> scene_world = get_world_3d();
    if (!scene_world.is_valid()) {
        return false;
    }
    if (reflection_viewport->get_world_3d() != scene_world) {
        reflection_viewport->set_world_3d(scene_world);
    }

    if (Engine::get_singleton()->is_editor_hint()) {
        resolve_editor_camera();
    } else if (!main_camera && main_camera_id.is_valid()) {
        // The exported camera pointer is cleared when the camera exits the tree;
        // recover it here after a tree re-entry (reparent, scene tab switch).
        main_camera = Object::cast_to<Camera3D>(ObjectDB::get_instance(main_camera_id));
        const Callable exit_handler = callable_mp(this, &PlanarReflectorClippingCPP::_on_main_camera_exiting);
        const Callable enter_handler = callable_mp(this, &PlanarReflectorClippingCPP::_on_main_camera_entered);
        if (main_camera && !main_camera->is_connected("tree_exiting", exit_handler)) {
            main_camera->connect("tree_exiting", exit_handler);
        }
        if (main_camera && !main_camera->is_connected("tree_entered", enter_handler)) {
            main_camera->connect("tree_entered", enter_handler);
        }
    }

    Camera3D *source = get_source_camera();
    if (!source) {
        warn_missing_runtime_camera();
        return false;
    }

    if (global_water_clipping_enabled && ensure_global_parameters()) {
        register_clipping_participant();
        if (!shared_marker_initialized) {
            publish_global_marker(true);
        }
        update_active_reflector_selection(source, true);
    }
    configure_reflection_camera();
    sync_all(source);
    bind_reflection_texture();
    setup_complete = true;
    apply_activity_state();
    request_reflection_render();
    update_configuration_warnings();
    return true;
}

void PlanarReflectorClippingCPP::create_runtime_nodes() {
    if (reflection_viewport && reflection_camera) {
        return;
    }

    reflection_viewport = memnew(SubViewport);
    reflection_viewport->set_name("ClippingReflectionViewport");
    reflection_viewport->set_use_own_world_3d(false);
    reflection_viewport->set_transparent_background(true);
    reflection_viewport->set_handle_input_locally(false);
    reflection_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);
    reflection_viewport->set_positional_shadow_atlas_size(shadow_atlas_size);
    reflection_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
    add_child(reflection_viewport);

    reflection_camera = memnew(Camera3D);
    reflection_camera->set_name("ClippingReflectionCamera");
    reflection_viewport->add_child(reflection_camera);
    reflection_camera->set_current(true);

    // Fresh camera and viewport: every cached value must be written once.
    last_projection_type = -1;
    last_fov = -1.0;
    last_ortho_size = -1.0;
    last_near = -1.0;
    last_far = -1.0;
    last_keep_aspect = -1;
    last_h_offset = 1.0e30;
    last_v_offset = 1.0e30;
    last_frustum_offset = Vector2(1.0e30, 1.0e30);
    last_cull_mask = 0xFFFFFFFFu;
    last_viewport_size = Vector2i();

    setup_environment();
}

void PlanarReflectorClippingCPP::destroy_runtime_nodes() {
    setup_generation++;
    setup_complete = false;
    clear_reflection_texture();
    if (reflection_viewport) {
        reflection_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
        if (reflection_viewport->is_inside_tree() && reflection_viewport->get_parent()) {
            reflection_viewport->get_parent()->remove_child(reflection_viewport);
        }
        reflection_viewport->queue_free();
    }
    reflection_camera = nullptr;
    reflection_viewport = nullptr;
}

void PlanarReflectorClippingCPP::_exit_tree() {
    // TEMP (req 9.9 measurement): remove after Godot 4.6 lifecycle verification.
    if (Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("[PRClip TEMP] EXIT_TREE: ", get_name());
    }
    unregister_clipping_participant();
    const Callable editor_exit_handler = callable_mp(this, &PlanarReflectorClippingCPP::_on_editor_camera_exiting);
    if (editor_camera && editor_camera->is_connected("tree_exiting", editor_exit_handler)) {
        editor_camera->disconnect("tree_exiting", editor_exit_handler);
    }
    editor_camera = nullptr; // Editor-owned; re-resolved at the next setup.
    destroy_runtime_nodes();
    set_process(false);
}

void PlanarReflectorClippingCPP::apply_activity_state() {
    const bool running = is_active && setup_complete;
    set_process(running);
    if (reflection_viewport) {
        reflection_viewport->set_update_mode(running ? SubViewport::UPDATE_ONCE : SubViewport::UPDATE_DISABLED);
    }
}

// ---------------------------------------------------------------------------
// _process: only the cadenced reflection render (req 12.5/12.6). Runs only on an
// initialized, active reflector - see apply_activity_state().
// ---------------------------------------------------------------------------

void PlanarReflectorClippingCPP::_process(double p_delta) {
    (void)p_delta;
    // Defensive guard for the window between Godot auto-enabling processing at
    // _ready and our deferred setup applying the real state.
    if (!setup_complete || !is_active) {
        return;
    }
    Camera3D *source = get_source_camera();
    if (!source) {
        return;
    }
    update_active_reflector_selection(source);
    frame_counter++;
    if (frame_counter % update_frequency != 0) {
        return;
    }
    Ref<Material> active_material = get_active_material(0);
    if (!bound_reflector_material.is_valid() || active_material.ptr() != bound_reflector_material.ptr()) {
        // MeshInstance3D has no material-changed signal. This O(1) identity check
        // detects an inspector/script material replacement without scanning anything.
        bind_reflection_texture();
    }
    sync_projection(source);
    sync_viewport_size(source);
    sync_reflection_transform(source);
    request_reflection_render();
}

// ---------------------------------------------------------------------------
// Cameras
// ---------------------------------------------------------------------------

Camera3D *PlanarReflectorClippingCPP::get_source_camera() const {
    if (Engine::get_singleton()->is_editor_hint()) {
        return editor_camera ? editor_camera : main_camera;
    }
    return main_camera;
}

void PlanarReflectorClippingCPP::resolve_editor_camera() {
    if (!Engine::get_singleton()->is_editor_hint() || !is_inside_tree()) {
        return;
    }
    EditorInterface *editor = EditorInterface::get_singleton();
    if (!editor) {
        return;
    }
    SubViewport *editor_viewport = editor->get_editor_viewport_3d(0);
    if (!editor_viewport) {
        return;
    }
    Camera3D *camera = editor_viewport->get_camera_3d();
    if (camera == editor_camera) {
        return;
    }
    const Callable exit_handler = callable_mp(this, &PlanarReflectorClippingCPP::_on_editor_camera_exiting);
    if (editor_camera && editor_camera->is_connected("tree_exiting", exit_handler)) {
        editor_camera->disconnect("tree_exiting", exit_handler);
    }
    editor_camera = camera;
    selection_dirty = true;
    if (editor_camera && !editor_camera->is_connected("tree_exiting", exit_handler)) {
        editor_camera->connect("tree_exiting", exit_handler);
    }
}

void PlanarReflectorClippingCPP::_on_editor_camera_exiting() {
    editor_camera = nullptr;
    // Re-resolve once after the editor finishes rebuilding its viewports.
    callable_mp(this, &PlanarReflectorClippingCPP::resolve_editor_camera).call_deferred();
}

void PlanarReflectorClippingCPP::set_main_camera(Camera3D *p_camera) {
    // Disconnect the previously connected camera. Resolve it by ID: the raw pointer
    // may already be cleared by _on_main_camera_exiting.
    const Callable exit_handler = callable_mp(this, &PlanarReflectorClippingCPP::_on_main_camera_exiting);
    const Callable enter_handler = callable_mp(this, &PlanarReflectorClippingCPP::_on_main_camera_entered);
    Camera3D *old_camera = Object::cast_to<Camera3D>(ObjectDB::get_instance(main_camera_id));
    if (old_camera && old_camera != p_camera) {
        if (old_camera->is_connected("tree_exiting", exit_handler)) {
            old_camera->disconnect("tree_exiting", exit_handler);
        }
        if (old_camera->is_connected("tree_entered", enter_handler)) {
            old_camera->disconnect("tree_entered", enter_handler);
        }
    }
    main_camera = p_camera;
    selection_dirty = true;
    main_camera_id = p_camera ? ObjectID(p_camera->get_instance_id()) : ObjectID();
    if (main_camera) {
        missing_camera_warned = false;
        if (!main_camera->is_connected("tree_exiting", exit_handler)) {
            main_camera->connect("tree_exiting", exit_handler);
        }
        if (!main_camera->is_connected("tree_entered", enter_handler)) {
            main_camera->connect("tree_entered", enter_handler);
        }
    }
    if (!is_inside_tree()) {
        return;
    }
    if (setup_complete) {
        Camera3D *source = get_source_camera();
        if (source) {
            sync_all(source);
            request_reflection_render();
        }
        apply_activity_state();
    } else {
        schedule_setup();
    }
    warn_missing_runtime_camera(); // No-op when a source camera exists.
    update_configuration_warnings();
}

Camera3D *PlanarReflectorClippingCPP::get_main_camera() const {
    return main_camera;
}

void PlanarReflectorClippingCPP::_on_main_camera_exiting() {
    // Keep main_camera_id: if the camera merely left the tree (reparent/tab switch)
    // it is recovered at the next setup; if it was freed, the ID resolves to null.
    main_camera = nullptr;
    if (!get_source_camera()) {
        setup_complete = false;
        apply_activity_state();
    }
    // Deferred so normal whole-scene teardown (we leave the tree too) stays silent;
    // only an individually removed/freed camera produces the warning.
    callable_mp(this, &PlanarReflectorClippingCPP::warn_missing_runtime_camera).call_deferred();
    update_configuration_warnings();
}

void PlanarReflectorClippingCPP::_on_main_camera_entered() {
    if (!main_camera_id.is_valid()) {
        return;
    }
    main_camera = Object::cast_to<Camera3D>(ObjectDB::get_instance(main_camera_id));
    if (!main_camera || !is_inside_tree() || !is_active || Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    missing_camera_warned = false;
    schedule_setup();
    update_configuration_warnings();
}

void PlanarReflectorClippingCPP::warn_missing_runtime_camera() {
    if (!is_inside_tree() || Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    if (get_source_camera()) {
        return;
    }
    set_process(false);
    if (!missing_camera_warned) {
        UtilityFunctions::push_warning("[PlanarReflectorClippingCPP] '", get_name(), "': main_camera is not assigned, is null, or was removed - reflections stay disabled until a valid Camera3D is set on the main_camera property.");
        missing_camera_warned = true;
    }
    update_configuration_warnings();
}

// ---------------------------------------------------------------------------
// Reflection synchronization (change-only writes, req 12.7)
// ---------------------------------------------------------------------------

void PlanarReflectorClippingCPP::sync_all(Camera3D *p_source) {
    sync_projection(p_source);
    sync_viewport_size(p_source);
    sync_reflection_transform(p_source);
}

void PlanarReflectorClippingCPP::sync_projection(Camera3D *p_source) {
    if (!p_source || !reflection_camera) {
        return;
    }
    if (auto_detect_camera_mode) {
        const int projection = int(p_source->get_projection());
        if (projection != last_projection_type) {
            reflection_camera->set_projection(Camera3D::ProjectionType(projection));
            last_projection_type = projection;
        }
    }
    const double near_plane = p_source->get_near();
    if (near_plane != last_near) {
        reflection_camera->set_near(near_plane);
        last_near = near_plane;
    }
    const double far_plane = p_source->get_far();
    if (far_plane != last_far) {
        reflection_camera->set_far(far_plane);
        last_far = far_plane;
    }
    const int keep_aspect = int(p_source->get_keep_aspect_mode());
    if (keep_aspect != last_keep_aspect) {
        reflection_camera->set_keep_aspect_mode(Camera3D::KeepAspect(keep_aspect));
        last_keep_aspect = keep_aspect;
    }
    const double h_offset = p_source->get_h_offset();
    if (h_offset != last_h_offset) {
        reflection_camera->set_h_offset(h_offset);
        last_h_offset = h_offset;
    }
    const double v_offset = p_source->get_v_offset();
    if (v_offset != last_v_offset) {
        reflection_camera->set_v_offset(v_offset);
        last_v_offset = v_offset;
    }
    const Vector2 frustum_offset = p_source->get_frustum_offset();
    if (frustum_offset != last_frustum_offset) {
        reflection_camera->set_frustum_offset(frustum_offset);
        last_frustum_offset = frustum_offset;
    }
    const double fov = p_source->get_fov();
    if (fov != last_fov) {
        reflection_camera->set_fov(fov);
        last_fov = fov;
    }
    const double ortho_size = p_source->get_size() * ortho_scale_multiplier;
    if (ortho_size != last_ortho_size) {
        reflection_camera->set_size(ortho_size);
        last_ortho_size = ortho_size;
    }
    if (reflection_camera->get_attributes() != p_source->get_attributes()) {
        reflection_camera->set_attributes(p_source->get_attributes());
    }
}

Vector2i PlanarReflectorClippingCPP::calculate_target_size(Camera3D *p_source) const {
    Vector2i size = fixed_reflection_resolution;
    if (resolution_mode != 2 && p_source && p_source->get_viewport()) {
        size = Vector2i(p_source->get_viewport()->get_visible_rect().size);
        double scale = resolution_mode == 1 ? resolution_scale : 1.0;
        if (use_lod) {
            double distance = get_global_position().distance_to(p_source->get_global_position());
            double denominator = Math::max(lod_distance_far - lod_distance_near, 0.001);
            double blend = Math::clamp((distance - lod_distance_near) / denominator, 0.0, 1.0);
            scale *= Math::lerp(1.0, lod_resolution_multiplier, blend);
        }
        size = Vector2i(int(size.x * scale), int(size.y * scale));
    }
    size.x = Math::max(size.x, 128);
    size.y = Math::max(size.y, 128);
    return size;
}

void PlanarReflectorClippingCPP::sync_viewport_size(Camera3D *p_source) {
    if (!reflection_viewport) {
        return;
    }
    Vector2i target = calculate_target_size(p_source);
    if (target != last_viewport_size) {
        reflection_viewport->set_size(target);
        last_viewport_size = target;
    }
}

Transform3D PlanarReflectorClippingCPP::apply_reflection_offset_to(const Transform3D &p_transform) const {
    if (!enable_reflection_offset) {
        return p_transform;
    }
    Basis offset_basis;
    offset_basis = offset_basis.rotated(Vector3(1, 0, 0), Math::deg_to_rad(reflection_offset_rotation.x));
    offset_basis = offset_basis.rotated(Vector3(0, 1, 0), Math::deg_to_rad(reflection_offset_rotation.y));
    offset_basis = offset_basis.rotated(Vector3(0, 0, 1), Math::deg_to_rad(reflection_offset_rotation.z));
    Transform3D offset(offset_basis, reflection_offset_position * reflection_offset_scale);
    Transform3D result = p_transform;
    if (offset_blend_mode == 1) {
        return result * offset;
    }
    if (offset_blend_mode == 2) {
        Camera3D *source = get_source_camera();
        if (source) {
            result.origin += source->get_global_basis().xform(offset.origin);
        }
    } else {
        result.origin += offset.origin;
    }
    result.basis = result.basis * offset.basis;
    return result;
}

void PlanarReflectorClippingCPP::sync_reflection_transform(Camera3D *p_source) {
    if (!p_source || !reflection_camera) {
        return;
    }
    const double water_height = get_global_position().y;
    Transform3D source_transform = p_source->get_global_transform();
    Vector3 reflected_position = source_transform.origin;
    reflected_position.y = water_height * 2.0 - reflected_position.y;

    Vector3 reflected_forward = -source_transform.basis.get_column(2);
    reflected_forward.y = -reflected_forward.y;
    Vector3 reflected_up = source_transform.basis.get_column(1);
    reflected_up.y = -reflected_up.y;

    reflection_camera->look_at_from_position(reflected_position, reflected_position + reflected_forward, reflected_up);
    reflection_camera->set_global_transform(apply_reflection_offset_to(reflection_camera->get_global_transform()));
}

void PlanarReflectorClippingCPP::request_reflection_render() {
    if (reflection_viewport && is_active && setup_complete) {
        reflection_viewport->set_update_mode(SubViewport::UPDATE_ONCE);
    }
}

// ---------------------------------------------------------------------------
// Environment, cull mask, texture binding
// ---------------------------------------------------------------------------

void PlanarReflectorClippingCPP::setup_environment() {
    if (!reflection_camera) {
        return;
    }
    if (use_custom_environment && custom_environment.is_valid()) {
        reflection_camera->set_environment(custom_environment);
    } else {
        // Same default reflection environment the legacy PlanarReflectorCPP builds.
        Ref<Environment> reflection_env;
        reflection_env.instantiate();
        reflection_env->set_background(Environment::BG_CLEAR_COLOR);
        reflection_env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
        reflection_env->set_ambient_light_color(Color(0.8, 0.8, 0.8));
        reflection_env->set_ambient_light_energy(1.0);
        reflection_camera->set_environment(reflection_env);
    }
}

uint32_t PlanarReflectorClippingCPP::get_marker_bit() const {
    return uint32_t(1) << uint32_t(Math::clamp(reflection_camera_marker_layer, 1, 20) - 1);
}

uint32_t PlanarReflectorClippingCPP::get_effective_reflection_mask() const {
    // The shared marker is reserved for camera identification, never object
    // visibility. Remove it from the artist mask, then add it only to enabled
    // clipping cameras.
    uint32_t result = uint32_t(reflection_layers) & ~shared_marker_bit;
    if (global_water_clipping_enabled) {
        result |= shared_marker_bit;
    }
    return result;
}

void PlanarReflectorClippingCPP::configure_reflection_camera() {
    if (!reflection_camera) {
        return;
    }
    const uint32_t mask = get_effective_reflection_mask();
    if (mask != last_cull_mask) {
        reflection_camera->set_cull_mask(mask);
        last_cull_mask = mask;
    }
}

void PlanarReflectorClippingCPP::bind_reflection_texture() {
    if (!reflection_viewport) {
        return;
    }
    Ref<Material> material = get_active_material(0);
    Ref<ShaderMaterial> shader = material;
    if (!shader.is_valid()) {
        clear_reflection_texture();
        return;
    }

    if (!bound_reflector_material.is_valid() || shader.ptr() != bound_reflector_material.ptr()) {
        clear_reflection_texture();

        // Every reflector gets a private material resource. This prevents two
        // reflectors sharing an artist material from overwriting or clearing each
        // other's reflection texture. The duplicate retains all artist parameters.
        Ref<Resource> duplicated_resource = shader->duplicate(false);
        Ref<ShaderMaterial> private_shader = duplicated_resource;
        if (!private_shader.is_valid()) {
            return;
        }
        private_shader->set_local_to_scene(true);
        set_surface_override_material(0, private_shader);
        bound_reflector_material = private_shader;
    }

    Ref<ViewportTexture> texture = reflection_viewport->get_texture();
    bound_reflector_material->set_shader_parameter("reflection_screen_texture", texture);
    bound_reflector_material->set_shader_parameter("reflection_texture", texture);
    bound_reflector_material->set_shader_parameter("reflection_flip_x", true);
    const bool debug_selected = is_selection_debug_enabled() && ObjectID(get_instance_id()) == selected_reflector_id;
    bound_reflector_material->set_shader_parameter("debug_selection_highlight", debug_selected ? 1.0 : 0.0);
    editor_texture_binding_suspended = false;
}

void PlanarReflectorClippingCPP::clear_reflection_texture() {
    if (bound_reflector_material.is_valid()) {
        bound_reflector_material->set_shader_parameter("reflection_screen_texture", Variant());
        bound_reflector_material->set_shader_parameter("reflection_texture", Variant());
    }
    bound_reflector_material.unref();
    editor_texture_binding_suspended = false;
}

void PlanarReflectorClippingCPP::restore_reflection_texture_after_save() {
    if (!editor_texture_binding_suspended || !is_inside_tree() || !reflection_viewport) {
        editor_texture_binding_suspended = false;
        return;
    }
    Ref<ViewportTexture> texture = reflection_viewport->get_texture();
    if (bound_reflector_material.is_valid()) {
        bound_reflector_material->set_shader_parameter("reflection_screen_texture", texture);
        bound_reflector_material->set_shader_parameter("reflection_texture", texture);
    }
    editor_texture_binding_suspended = false;
    update_selection_debug_visuals();
}

// ---------------------------------------------------------------------------
// Global shader parameters and camera-selected shared water clipping
// ---------------------------------------------------------------------------

void PlanarReflectorClippingCPP::ensure_global_shader_parameters() {
    // Runs once at module init (SCENE level), before any scene resource loads:
    // shaders declaring these global uniforms fail to compile if the globals do
    // not exist yet, so node-level creation would always be too late.
    //
    // The durable mechanism is Project Settings (shader_globals/...): the engine
    // itself registers those at startup - editor, game, and exports alike. The
    // editor session additionally adds them live via RenderingServer and persists
    // them into project.godot the first time, so every later run has them natively.
    if (global_parameters_initialized) {
        return;
    }
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (!settings) {
        return;
    }
    const bool height_in_project = settings->has_setting("shader_globals/planar_water_height");
    const bool bit_in_project = settings->has_setting("shader_globals/planar_reflection_camera_bit");
    if (height_in_project && bit_in_project) {
        global_parameters_initialized = true; // Engine registers them from project.godot.
        return;
    }

    // RenderingServer is not yet registered at runtime module init (it is in the
    // editor). Check first to avoid error spam; node setup retries as a fallback.
    if (!Engine::get_singleton()->has_singleton("RenderingServer")) {
        return;
    }
    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) {
        return;
    }
    const StringName height_name(WATER_HEIGHT_GLOBAL);
    const StringName bit_name(CAMERA_BIT_GLOBAL);
    const int64_t default_marker_bit = int64_t(uint32_t(1) << 19); // Layer 20.

    if (Engine::get_singleton()->is_editor_hint()) {
        if (!height_in_project) {
            if (rs->global_shader_parameter_get_type(height_name) == RenderingServer::GLOBAL_VAR_TYPE_MAX) {
                rs->global_shader_parameter_add(height_name, RenderingServer::GLOBAL_VAR_TYPE_FLOAT, 0.0);
            }
            Dictionary height_def;
            height_def["type"] = "float";
            height_def["value"] = 0.0;
            settings->set_setting("shader_globals/planar_water_height", height_def);
        }
        if (!bit_in_project) {
            if (rs->global_shader_parameter_get_type(bit_name) == RenderingServer::GLOBAL_VAR_TYPE_MAX) {
                rs->global_shader_parameter_add(bit_name, RenderingServer::GLOBAL_VAR_TYPE_UINT, default_marker_bit);
            }
            Dictionary bit_def;
            bit_def["type"] = "uint";
            bit_def["value"] = default_marker_bit;
            settings->set_setting("shader_globals/planar_reflection_camera_bit", bit_def);
        }
        if (settings->save() != OK) {
            UtilityFunctions::push_warning("[PlanarReflectorClippingCPP] Could not save project settings; the clipping shader globals were added for this session only.");
        }
    } else {
        // Runtime fallback for projects that never opened the editor with this
        // addon enabled (type/list introspection is editor-only in Godot).
        if (!height_in_project) {
            rs->global_shader_parameter_add(height_name, RenderingServer::GLOBAL_VAR_TYPE_FLOAT, 0.0);
        }
        if (!bit_in_project) {
            rs->global_shader_parameter_add(bit_name, RenderingServer::GLOBAL_VAR_TYPE_UINT, default_marker_bit);
        }
    }
    global_parameters_initialized = true;
}

void PlanarReflectorClippingCPP::shutdown_shared_manager() {
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        rs->global_shader_parameter_set_override(StringName(WATER_HEIGHT_GLOBAL), Variant());
        rs->global_shader_parameter_set_override(StringName(CAMERA_BIT_GLOBAL), Variant());
    }
    clipping_participants.clear();
    last_height_writer_id = ObjectID();
    last_marker_writer_id = ObjectID();
    selected_reflector_id = ObjectID();
    shared_height_initialized = false;
    shared_marker_initialized = false;
    selection_dirty = true;
    selection_camera_cache_valid = false;
    selection_camera_id = ObjectID();
    last_selection_frame = uint64_t(-1);
}

bool PlanarReflectorClippingCPP::ensure_global_parameters() {
    ensure_global_shader_parameters(); // Idempotent fallback; normally done at module init.
    if (!global_parameters_initialized) {
        return false;
    }
    if (Engine::get_singleton()->is_editor_hint()) {
        // Validate types so a project-defined global with the wrong type produces a
        // clear warning instead of silent breakage (req 16.4). Event-time only.
        RenderingServer *rs = RenderingServer::get_singleton();
        const RenderingServer::GlobalShaderParameterType height_type = rs->global_shader_parameter_get_type(StringName(WATER_HEIGHT_GLOBAL));
        const RenderingServer::GlobalShaderParameterType bit_type = rs->global_shader_parameter_get_type(StringName(CAMERA_BIT_GLOBAL));
        global_parameter_conflict = (height_type != RenderingServer::GLOBAL_VAR_TYPE_FLOAT) || (bit_type != RenderingServer::GLOBAL_VAR_TYPE_UINT);
        if (global_parameter_conflict) {
            if (!global_conflict_warned) {
                UtilityFunctions::push_warning("[PlanarReflectorClippingCPP] Global shader parameters 'planar_water_height'/'planar_reflection_camera_bit' exist with conflicting types in Project Settings. Water clipping stays disabled until their types are float/uint.");
                global_conflict_warned = true;
            }
            return false;
        }
    }
    return true;
}

void PlanarReflectorClippingCPP::register_clipping_participant() {
    const ObjectID id(get_instance_id());
    if (!clipping_participants.has(id)) {
        clipping_participants.push_back(id);
    }
    selection_dirty = true;
}

void PlanarReflectorClippingCPP::unregister_clipping_participant() {
    const ObjectID id(get_instance_id());
    clipping_participants.erase(id);
    if (last_height_writer_id == id) {
        last_height_writer_id = ObjectID();
    }
    if (last_marker_writer_id == id) {
        last_marker_writer_id = ObjectID();
    }
    if (selected_reflector_id == id) {
        selected_reflector_id = ObjectID();
    }
    if (clipping_participants.is_empty()) {
        RenderingServer *rs = RenderingServer::get_singleton();
        if (rs) {
            // Restore the project-wide defaults when this subsystem is no longer active.
            rs->global_shader_parameter_set_override(StringName(WATER_HEIGHT_GLOBAL), Variant());
            rs->global_shader_parameter_set_override(StringName(CAMERA_BIT_GLOBAL), Variant());
        }
        shared_height_initialized = false;
        shared_marker_initialized = false;
    }
    selection_dirty = true;
    refresh_participant_cameras_and_warnings();
}

void PlanarReflectorClippingCPP::publish_global_height(bool p_force) {
    if (!global_water_clipping_enabled || !is_active) {
        return;
    }
    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) {
        return;
    }
    const double height = get_global_position().y;
    const ObjectID id(get_instance_id());
    if (p_force || last_height_writer_id != id || !Math::is_equal_approx(height, shared_water_height)) {
        const StringName height_name(WATER_HEIGHT_GLOBAL);
        rs->global_shader_parameter_set(height_name, height);
        // The clipping subsystem owns the effective runtime/editor value while at
        // least one clipping reflector is active. This bypasses a scene-level
        // ShaderGlobalsOverride or editor override retaining the project default.
        rs->global_shader_parameter_set_override(height_name, height);
        shared_water_height = height;
        shared_height_initialized = true;
        last_height_writer_id = id;
        if (is_selection_debug_enabled()) {
            const Variant applied_height = rs->global_shader_parameter_get(WATER_HEIGHT_GLOBAL);
            UtilityFunctions::print("[PRClip Height] global planar_water_height=", height,
                    " readback=", applied_height,
                    " writer=", get_name(), " force=", p_force);
        }
        for (int i = 0; i < clipping_participants.size(); i++) {
            PlanarReflectorClippingCPP *participant = Object::cast_to<PlanarReflectorClippingCPP>(ObjectDB::get_instance(clipping_participants[i]));
            if (participant) {
                participant->update_configuration_warnings();
                // Every compatible object shader in every reflection viewport reads
                // this shared value, so a height change invalidates every reflection.
                participant->request_reflection_render();
            }
        }
    }
}

void PlanarReflectorClippingCPP::publish_global_marker(bool p_force) {
    if (!global_water_clipping_enabled || !is_active) {
        return;
    }
    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) {
        return;
    }
    const uint32_t marker = get_marker_bit();
    const ObjectID id(get_instance_id());
    if (p_force || last_marker_writer_id != id || marker != shared_marker_bit) {
        const StringName marker_name(CAMERA_BIT_GLOBAL);
        rs->global_shader_parameter_set(marker_name, int64_t(marker));
        rs->global_shader_parameter_set_override(marker_name, int64_t(marker));
        shared_marker_bit = marker;
        shared_marker_initialized = true;
        last_marker_writer_id = id;
        refresh_participant_cameras_and_warnings();
    }
}

void PlanarReflectorClippingCPP::refresh_participant_cameras_and_warnings() {
    for (int i = clipping_participants.size() - 1; i >= 0; i--) {
        PlanarReflectorClippingCPP *participant = Object::cast_to<PlanarReflectorClippingCPP>(ObjectDB::get_instance(clipping_participants[i]));
        if (!participant || participant->is_queued_for_deletion()) {
            clipping_participants.remove_at(i);
            continue;
        }
        participant->configure_reflection_camera();
        participant->update_configuration_warnings();
    }
}

bool PlanarReflectorClippingCPP::is_selection_debug_enabled() {
    for (int i = 0; i < clipping_participants.size(); i++) {
        PlanarReflectorClippingCPP *participant = Object::cast_to<PlanarReflectorClippingCPP>(ObjectDB::get_instance(clipping_participants[i]));
        if (participant && !participant->is_queued_for_deletion() && participant->debug_selection_diagnostics) {
            return true;
        }
    }
    return false;
}

void PlanarReflectorClippingCPP::update_selection_debug_visuals() {
    const bool debug_enabled = is_selection_debug_enabled();
    for (int i = 0; i < clipping_participants.size(); i++) {
        PlanarReflectorClippingCPP *participant = Object::cast_to<PlanarReflectorClippingCPP>(ObjectDB::get_instance(clipping_participants[i]));
        if (!participant || participant->is_queued_for_deletion() || !participant->bound_reflector_material.is_valid()) {
            continue;
        }
        const bool selected = debug_enabled && clipping_participants[i] == selected_reflector_id;
        participant->bound_reflector_material->set_shader_parameter("debug_selection_highlight", selected ? 1.0 : 0.0);
    }
}

bool PlanarReflectorClippingCPP::aabb_intersects_camera_frustum(const AABB &p_world_aabb, Camera3D *p_camera) {
    if (!p_camera) {
        return false;
    }
    const TypedArray<Plane> frustum = p_camera->get_frustum();
    for (int plane_index = 0; plane_index < frustum.size(); plane_index++) {
        const Plane plane = frustum[plane_index];
        bool every_corner_outside = true;
        for (int corner_index = 0; corner_index < 8; corner_index++) {
            if (!plane.is_point_over(p_world_aabb.get_endpoint(corner_index))) {
                every_corner_outside = false;
                break;
            }
        }
        if (every_corner_outside) {
            return false;
        }
    }
    return true;
}

double PlanarReflectorClippingCPP::distance_squared_to_aabb(const Vector3 &p_point, const AABB &p_aabb) {
    const Vector3 end = p_aabb.position + p_aabb.size;
    const Vector3 closest(
            Math::clamp(p_point.x, p_aabb.position.x, end.x),
            Math::clamp(p_point.y, p_aabb.position.y, end.y),
            Math::clamp(p_point.z, p_aabb.position.z, end.z));
    return double(p_point.distance_squared_to(closest));
}

void PlanarReflectorClippingCPP::update_active_reflector_selection(Camera3D *p_source_camera, bool p_force) {
    if (!p_source_camera || p_source_camera->is_queued_for_deletion() || !p_source_camera->is_inside_tree()) {
        return;
    }

    const uint64_t frame = Engine::get_singleton()->get_process_frames();
    if (!p_force && last_selection_frame == frame) {
        return;
    }
    last_selection_frame = frame;

    Vector2i viewport_size;
    if (p_source_camera->get_viewport()) {
        viewport_size = Vector2i(p_source_camera->get_viewport()->get_visible_rect().size);
    }
    const ObjectID camera_id(p_source_camera->get_instance_id());
    const Transform3D camera_transform = p_source_camera->get_global_transform();
    const int camera_projection = int(p_source_camera->get_projection());
    const double camera_fov = p_source_camera->get_fov();
    const double camera_size = p_source_camera->get_size();
    const double camera_near = p_source_camera->get_near();
    const double camera_far = p_source_camera->get_far();
    const double camera_h_offset = p_source_camera->get_h_offset();
    const double camera_v_offset = p_source_camera->get_v_offset();
    const Vector2 camera_frustum_offset = p_source_camera->get_frustum_offset();
    const int camera_keep_aspect = int(p_source_camera->get_keep_aspect_mode());
    const uint32_t camera_cull_mask = p_source_camera->get_cull_mask();
    const bool camera_changed = !selection_camera_cache_valid
            || selection_camera_id != camera_id
            || selection_camera_transform != camera_transform
            || selection_camera_projection != camera_projection
            || !Math::is_equal_approx(selection_camera_fov, camera_fov)
            || !Math::is_equal_approx(selection_camera_size, camera_size)
            || !Math::is_equal_approx(selection_camera_near, camera_near)
            || !Math::is_equal_approx(selection_camera_far, camera_far)
            || !Math::is_equal_approx(selection_camera_h_offset, camera_h_offset)
            || !Math::is_equal_approx(selection_camera_v_offset, camera_v_offset)
            || selection_camera_frustum_offset != camera_frustum_offset
            || selection_camera_keep_aspect != camera_keep_aspect
            || selection_camera_cull_mask != camera_cull_mask
            || selection_camera_viewport_size != viewport_size;
    if (!p_force && !selection_dirty && !camera_changed) {
        return;
    }

    selection_camera_cache_valid = true;
    selection_camera_id = camera_id;
    selection_camera_transform = camera_transform;
    selection_camera_projection = camera_projection;
    selection_camera_fov = camera_fov;
    selection_camera_size = camera_size;
    selection_camera_near = camera_near;
    selection_camera_far = camera_far;
    selection_camera_h_offset = camera_h_offset;
    selection_camera_v_offset = camera_v_offset;
    selection_camera_frustum_offset = camera_frustum_offset;
    selection_camera_keep_aspect = camera_keep_aspect;
    selection_camera_cull_mask = camera_cull_mask;
    selection_camera_viewport_size = viewport_size;
    selection_dirty = false;

    PlanarReflectorClippingCPP *best = nullptr;
    PlanarReflectorClippingCPP *current = nullptr;
    double best_distance_squared = 1.0e300;
    double current_distance_squared = 1.0e300;
    int eligible_count = 0;
    const Vector3 camera_position = p_source_camera->get_global_position();
    const Ref<World3D> camera_world = p_source_camera->get_world_3d();

    for (int i = clipping_participants.size() - 1; i >= 0; i--) {
        const ObjectID participant_id = clipping_participants[i];
        PlanarReflectorClippingCPP *participant = Object::cast_to<PlanarReflectorClippingCPP>(ObjectDB::get_instance(participant_id));
        if (!participant || participant->is_queued_for_deletion() || !participant->is_inside_tree()) {
            clipping_participants.remove_at(i);
            continue;
        }
        if (!participant->is_active || !participant->global_water_clipping_enabled || !participant->is_visible_in_tree()) {
            continue;
        }
        if (participant->get_source_camera() != p_source_camera || participant->get_world_3d() != camera_world) {
            continue;
        }
        if ((participant->get_layer_mask() & p_source_camera->get_cull_mask()) == 0) {
            continue;
        }

        const AABB world_aabb = participant->get_global_transform().xform(participant->get_aabb());
        if (!aabb_intersects_camera_frustum(world_aabb, p_source_camera)) {
            continue;
        }

        eligible_count++;
        const double distance_squared = distance_squared_to_aabb(camera_position, world_aabb);
        if (participant_id == selected_reflector_id) {
            current = participant;
            current_distance_squared = distance_squared;
        }
        if (!best || distance_squared < best_distance_squared) {
            best = participant;
            best_distance_squared = distance_squared;
        }
    }

    // Keep the current selection until a competitor is at least 10% closer.
    // Squared distances use 0.9 * 0.9 = 0.81.
    PlanarReflectorClippingCPP *selected = best;
    if (current && best && best != current && best_distance_squared >= current_distance_squared * 0.81) {
        selected = current;
    }

    if (!selected) {
        if (is_selection_debug_enabled()) {
            UtilityFunctions::print("[PRClip Selection] camera=", p_source_camera->get_name(),
                    " candidates=0 selected=<none> global_height_retained=", shared_water_height);
        }
        selected_reflector_id = ObjectID();
        refresh_participant_cameras_and_warnings();
        update_selection_debug_visuals();
        return;
    }

    const ObjectID new_selected_id(selected->get_instance_id());
    const bool selection_changed = new_selected_id != selected_reflector_id;
    if (is_selection_debug_enabled()) {
        if (current && best && best != current) {
            UtilityFunctions::print("[PRClip Selection] camera=", p_source_camera->get_name(),
                    " candidates=", eligible_count,
                    " current=", current->get_name(), " current_distance=", Math::sqrt(current_distance_squared),
                    " challenger=", best->get_name(), " challenger_distance=", Math::sqrt(best_distance_squared),
                    " result=", selected->get_name(),
                    selected == current ? " (hysteresis kept current)" : " (challenger selected)");
        } else {
            UtilityFunctions::print("[PRClip Selection] camera=", p_source_camera->get_name(),
                    " candidates=", eligible_count,
                    " selected=", selected->get_name(),
                    " distance=", Math::sqrt(best_distance_squared),
                    " height=", selected->get_global_position().y,
                    selection_changed ? " (selection changed)" : " (selection retained)");
        }
    }
    selected_reflector_id = new_selected_id;
    selected->publish_global_height(selection_changed || !shared_height_initialized);
    if (selection_changed) {
        refresh_participant_cameras_and_warnings();
        update_selection_debug_visuals();
    }
}

// ---------------------------------------------------------------------------
// Diagnostics (req 16.x) - derived from held state only, no scans.
// ---------------------------------------------------------------------------

PackedStringArray PlanarReflectorClippingCPP::_get_configuration_warnings() const {
    PackedStringArray warnings;
    if (!get_mesh().is_valid()) {
        warnings.push_back("Assign a reflector mesh (normally PlaneMesh).");
    }
    Ref<Material> material = get_active_material(0);
    Ref<ShaderMaterial> shader = material;
    if (!shader.is_valid()) {
        warnings.push_back("The reflector requires a ShaderMaterial that accepts reflection_texture or reflection_screen_texture.");
    }
    if (!Engine::get_singleton()->is_editor_hint() && !main_camera) {
        warnings.push_back("Assign main_camera for runtime reflections.");
    }
    if (global_water_clipping_enabled && !Math::is_equal_approx(double(get_global_position().y), shared_water_height)) {
        PlanarReflectorClippingCPP *selected = Object::cast_to<PlanarReflectorClippingCPP>(ObjectDB::get_instance(selected_reflector_id));
        String selected_name = selected ? String(selected->get_name()) : String("the last selected reflector");
        warnings.push_back(String("All clipping reflectors share one global water height. Current height is Y ")
                + String::num(shared_water_height) + String(", selected from the nearest in-frustum reflector '") + selected_name
                + String("'. This reflector is at Y ") + String::num(get_global_position().y)
                + String(" and continues rendering with the selected shared clipping height."));
    }
    if (global_water_clipping_enabled && get_marker_bit() != shared_marker_bit) {
        PlanarReflectorClippingCPP *marker_writer = Object::cast_to<PlanarReflectorClippingCPP>(ObjectDB::get_instance(last_marker_writer_id));
        String writer_name = marker_writer ? String(marker_writer->get_name()) : String("a previously active reflector");
        int current_marker_layer = 1;
        uint32_t marker_bits = shared_marker_bit;
        while (marker_bits > 1) {
            marker_bits >>= 1;
            current_marker_layer++;
        }
        warnings.push_back(String("All clipping reflectors share one camera marker. Current marker is layer ")
                + String::num_int64(current_marker_layer)
                + String(", last updated by '") + writer_name
                + String("'. Changing this reflector's marker will update every clipping reflector."));
    }
    if (global_water_clipping_enabled && (shared_marker_bit & uint32_t(reflection_layers)) != 0) {
        warnings.push_back("The shared reflection-camera marker overlaps reflection_layers. The marker is reserved for camera identification and is automatically excluded from reflected object layers.");
    }
    if (global_parameter_conflict) {
        warnings.push_back("Global shader parameters 'planar_water_height'/'planar_reflection_camera_bit' exist with conflicting types in Project Settings (need float/uint).");
    }
    return warnings;
}

// ---------------------------------------------------------------------------
// Setters / getters
// ---------------------------------------------------------------------------

void PlanarReflectorClippingCPP::set_is_active(bool p_value) {
    if (is_active == p_value) {
        return;
    }
    is_active = p_value;
    if (is_active) {
        if (is_inside_tree()) {
            schedule_setup();
        }
    } else {
        unregister_clipping_participant();
        apply_activity_state();
        configure_reflection_camera();
    }
}
bool PlanarReflectorClippingCPP::get_is_active() const { return is_active; }

void PlanarReflectorClippingCPP::set_global_water_clipping_enabled(bool p_value) {
    if (global_water_clipping_enabled == p_value) {
        return;
    }
    global_water_clipping_enabled = p_value;
    if (global_water_clipping_enabled) {
        if (is_inside_tree() && setup_complete) {
            register_clipping_participant();
            if (ensure_global_parameters()) {
                publish_global_marker(true);
                update_active_reflector_selection(get_source_camera(), true);
            }
        }
    } else {
        unregister_clipping_participant();
        configure_reflection_camera();
    }
    update_configuration_warnings();
}
bool PlanarReflectorClippingCPP::get_global_water_clipping_enabled() const { return global_water_clipping_enabled; }

void PlanarReflectorClippingCPP::set_debug_selection_diagnostics(bool p_value) {
    if (debug_selection_diagnostics == p_value) {
        return;
    }
    debug_selection_diagnostics = p_value;
    selection_dirty = true;
    update_selection_debug_visuals();
}
bool PlanarReflectorClippingCPP::get_debug_selection_diagnostics() const { return debug_selection_diagnostics; }

void PlanarReflectorClippingCPP::set_reflection_camera_marker_layer(int p_layer) {
    reflection_camera_marker_layer = Math::clamp(p_layer, 1, 20);
    if (global_water_clipping_enabled && is_inside_tree() && setup_complete) {
        publish_global_marker(true);
    }
    configure_reflection_camera();
    update_configuration_warnings();
}
int PlanarReflectorClippingCPP::get_reflection_camera_marker_layer() const { return reflection_camera_marker_layer; }

void PlanarReflectorClippingCPP::set_reflection_layers(int p_layers) {
    reflection_layers = p_layers;
    configure_reflection_camera();
    update_configuration_warnings();
}
int PlanarReflectorClippingCPP::get_reflection_layers() const { return reflection_layers; }
void PlanarReflectorClippingCPP::set_auto_detect_camera_mode(bool p_value) { auto_detect_camera_mode = p_value; }
bool PlanarReflectorClippingCPP::get_auto_detect_camera_mode() const { return auto_detect_camera_mode; }
void PlanarReflectorClippingCPP::set_ortho_scale_multiplier(double p_value) { ortho_scale_multiplier = Math::max(p_value, 0.01); }
double PlanarReflectorClippingCPP::get_ortho_scale_multiplier() const { return ortho_scale_multiplier; }

void PlanarReflectorClippingCPP::set_resolution_mode(int p_mode) { resolution_mode = Math::clamp(p_mode, 0, 2); last_viewport_size = Vector2i(); }
int PlanarReflectorClippingCPP::get_resolution_mode() const { return resolution_mode; }
void PlanarReflectorClippingCPP::set_resolution_scale(double p_scale) { resolution_scale = Math::clamp(p_scale, 0.1, 1.0); last_viewport_size = Vector2i(); }
double PlanarReflectorClippingCPP::get_resolution_scale() const { return resolution_scale; }
void PlanarReflectorClippingCPP::set_fixed_reflection_resolution(Vector2i p_size) { fixed_reflection_resolution = p_size.max(Vector2i(128, 128)); last_viewport_size = Vector2i(); }
Vector2i PlanarReflectorClippingCPP::get_fixed_reflection_resolution() const { return fixed_reflection_resolution; }
void PlanarReflectorClippingCPP::set_update_frequency(int p_frequency) { update_frequency = Math::max(p_frequency, 1); }
int PlanarReflectorClippingCPP::get_update_frequency() const { return update_frequency; }
void PlanarReflectorClippingCPP::set_shadow_atlas_size(int p_size) {
    shadow_atlas_size = Math::max(p_size, 0);
    if (reflection_viewport) {
        reflection_viewport->set_positional_shadow_atlas_size(shadow_atlas_size);
    }
}
int PlanarReflectorClippingCPP::get_shadow_atlas_size() const { return shadow_atlas_size; }

void PlanarReflectorClippingCPP::set_use_custom_environment(bool p_value) { use_custom_environment = p_value; setup_environment(); }
bool PlanarReflectorClippingCPP::get_use_custom_environment() const { return use_custom_environment; }
void PlanarReflectorClippingCPP::set_custom_environment(Environment *p_environment) { custom_environment = Ref<Environment>(p_environment); setup_environment(); }
Environment *PlanarReflectorClippingCPP::get_custom_environment() const { return custom_environment.ptr(); }
void PlanarReflectorClippingCPP::set_enable_reflection_offset(bool p_value) { enable_reflection_offset = p_value; }
bool PlanarReflectorClippingCPP::get_enable_reflection_offset() const { return enable_reflection_offset; }
void PlanarReflectorClippingCPP::set_reflection_offset_position(Vector3 p_value) { reflection_offset_position = p_value; }
Vector3 PlanarReflectorClippingCPP::get_reflection_offset_position() const { return reflection_offset_position; }
void PlanarReflectorClippingCPP::set_reflection_offset_rotation(Vector3 p_value) { reflection_offset_rotation = p_value; }
Vector3 PlanarReflectorClippingCPP::get_reflection_offset_rotation() const { return reflection_offset_rotation; }
void PlanarReflectorClippingCPP::set_reflection_offset_scale(double p_value) { reflection_offset_scale = p_value; }
double PlanarReflectorClippingCPP::get_reflection_offset_scale() const { return reflection_offset_scale; }
void PlanarReflectorClippingCPP::set_offset_blend_mode(int p_mode) { offset_blend_mode = Math::clamp(p_mode, 0, 2); }
int PlanarReflectorClippingCPP::get_offset_blend_mode() const { return offset_blend_mode; }

void PlanarReflectorClippingCPP::set_use_lod(bool p_value) { use_lod = p_value; last_viewport_size = Vector2i(); }
bool PlanarReflectorClippingCPP::get_use_lod() const { return use_lod; }
void PlanarReflectorClippingCPP::set_lod_distance_near(double p_value) { lod_distance_near = Math::max(p_value, 0.0); }
double PlanarReflectorClippingCPP::get_lod_distance_near() const { return lod_distance_near; }
void PlanarReflectorClippingCPP::set_lod_distance_far(double p_value) { lod_distance_far = Math::max(p_value, lod_distance_near + 0.001); }
double PlanarReflectorClippingCPP::get_lod_distance_far() const { return lod_distance_far; }
void PlanarReflectorClippingCPP::set_lod_resolution_multiplier(double p_value) { lod_resolution_multiplier = Math::clamp(p_value, 0.1, 1.0); }
double PlanarReflectorClippingCPP::get_lod_resolution_multiplier() const { return lod_resolution_multiplier; }

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void PlanarReflectorClippingCPP::_bind_methods() {
    ClassDB::bind_method(D_METHOD("deferred_setup", "generation"), &PlanarReflectorClippingCPP::deferred_setup);
    ClassDB::bind_method(D_METHOD("restore_reflection_texture_after_save"), &PlanarReflectorClippingCPP::restore_reflection_texture_after_save);

#define BIND_PROP(type, name, setter, getter, hint, hint_text) \
    ClassDB::bind_method(D_METHOD(#setter, "value"), &PlanarReflectorClippingCPP::setter); \
    ClassDB::bind_method(D_METHOD(#getter), &PlanarReflectorClippingCPP::getter); \
    ADD_PROPERTY(PropertyInfo(type, name, hint, hint_text), #setter, #getter)

    BIND_PROP(Variant::BOOL, "is_active", set_is_active, get_is_active, PROPERTY_HINT_NONE, "");
    ClassDB::bind_method(D_METHOD("set_main_camera", "camera"), &PlanarReflectorClippingCPP::set_main_camera);
    ClassDB::bind_method(D_METHOD("get_main_camera"), &PlanarReflectorClippingCPP::get_main_camera);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "main_camera", PROPERTY_HINT_NODE_TYPE, "Camera3D"), "set_main_camera", "get_main_camera");

    ADD_GROUP("Global Water Clipping", "");
    BIND_PROP(Variant::BOOL, "global_water_clipping_enabled", set_global_water_clipping_enabled, get_global_water_clipping_enabled, PROPERTY_HINT_NONE, "");
    BIND_PROP(Variant::INT, "reflection_camera_marker_layer", set_reflection_camera_marker_layer, get_reflection_camera_marker_layer, PROPERTY_HINT_RANGE, "1,20,1");

    ADD_GROUP("Diagnostics", "");
    BIND_PROP(Variant::BOOL, "debug_selection_diagnostics", set_debug_selection_diagnostics, get_debug_selection_diagnostics, PROPERTY_HINT_NONE, "");

    ADD_GROUP("Camera and Layers", "");
    BIND_PROP(Variant::INT, "reflection_layers", set_reflection_layers, get_reflection_layers, PROPERTY_HINT_LAYERS_3D_RENDER, "");
    BIND_PROP(Variant::BOOL, "auto_detect_camera_mode", set_auto_detect_camera_mode, get_auto_detect_camera_mode, PROPERTY_HINT_NONE, "");
    BIND_PROP(Variant::FLOAT, "ortho_scale_multiplier", set_ortho_scale_multiplier, get_ortho_scale_multiplier, PROPERTY_HINT_RANGE, "0.1,10.0,0.01");

    ADD_GROUP("Resolution and Updates", "");
    BIND_PROP(Variant::INT, "resolution_mode", set_resolution_mode, get_resolution_mode, PROPERTY_HINT_ENUM, "Match Viewport,Scaled Viewport,Fixed");
    BIND_PROP(Variant::FLOAT, "resolution_scale", set_resolution_scale, get_resolution_scale, PROPERTY_HINT_RANGE, "0.1,1.0,0.05");
    BIND_PROP(Variant::VECTOR2I, "fixed_reflection_resolution", set_fixed_reflection_resolution, get_fixed_reflection_resolution, PROPERTY_HINT_NONE, "");
    BIND_PROP(Variant::INT, "update_frequency", set_update_frequency, get_update_frequency, PROPERTY_HINT_RANGE, "1,10,1");
    BIND_PROP(Variant::INT, "shadow_atlas_size", set_shadow_atlas_size, get_shadow_atlas_size, PROPERTY_HINT_ENUM, "Disabled:0,1024:1024,2048:2048,4096:4096");

    ADD_GROUP("Environment", "");
    BIND_PROP(Variant::BOOL, "use_custom_environment", set_use_custom_environment, get_use_custom_environment, PROPERTY_HINT_NONE, "");
    ClassDB::bind_method(D_METHOD("set_custom_environment", "environment"), &PlanarReflectorClippingCPP::set_custom_environment);
    ClassDB::bind_method(D_METHOD("get_custom_environment"), &PlanarReflectorClippingCPP::get_custom_environment);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "custom_environment", PROPERTY_HINT_RESOURCE_TYPE, "Environment"), "set_custom_environment", "get_custom_environment");
    ADD_GROUP("Reflection Offset", "");
    BIND_PROP(Variant::BOOL, "enable_reflection_offset", set_enable_reflection_offset, get_enable_reflection_offset, PROPERTY_HINT_NONE, "");
    BIND_PROP(Variant::VECTOR3, "reflection_offset_position", set_reflection_offset_position, get_reflection_offset_position, PROPERTY_HINT_NONE, "");
    BIND_PROP(Variant::VECTOR3, "reflection_offset_rotation", set_reflection_offset_rotation, get_reflection_offset_rotation, PROPERTY_HINT_NONE, "");
    BIND_PROP(Variant::FLOAT, "reflection_offset_scale", set_reflection_offset_scale, get_reflection_offset_scale, PROPERTY_HINT_RANGE, "0.0,10.0,0.01");
    BIND_PROP(Variant::INT, "offset_blend_mode", set_offset_blend_mode, get_offset_blend_mode, PROPERTY_HINT_ENUM, "Add,Multiply,Camera Relative");

    ADD_GROUP("Distance LOD", "");
    BIND_PROP(Variant::BOOL, "use_lod", set_use_lod, get_use_lod, PROPERTY_HINT_NONE, "");
    BIND_PROP(Variant::FLOAT, "lod_distance_near", set_lod_distance_near, get_lod_distance_near, PROPERTY_HINT_RANGE, "0.0,1000.0,0.1");
    BIND_PROP(Variant::FLOAT, "lod_distance_far", set_lod_distance_far, get_lod_distance_far, PROPERTY_HINT_RANGE, "0.1,2000.0,0.1");
    BIND_PROP(Variant::FLOAT, "lod_resolution_multiplier", set_lod_resolution_multiplier, get_lod_resolution_multiplier, PROPERTY_HINT_RANGE, "0.1,1.0,0.05");

#undef BIND_PROP
}
