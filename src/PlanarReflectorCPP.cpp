#include "PlanarReflectorCPP.h" //Must have reference to our header .h file
#include "ReflectionEffectPrePass.h" // Include our C++ ReflectionEffectPrePass
//Must have include Godot base classes and can add more references as needed
#include <godot_cpp/core/class_db.hpp> 
#include <godot_cpp/godot.hpp> 
#include <godot_cpp/variant/utility_functions.hpp> 
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/camera_attributes.hpp>
#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

//Define your constructor (Must have)
PlanarReflectorCPP::PlanarReflectorCPP() 
{  
    // Initialize default values
    is_editor_setup_finished = false;
    is_active = true;
    reflection_camera_resolution = Vector2i(1920, 1080);
    ortho_scale_multiplier = 1.0;
    ortho_uv_scale = 1.0;
    auto_detect_camera_mode = true;
    reflection_layers = 1;
    use_custom_environment = true;
    
    // NEW: Compositor Effects initialization
    use_custom_compositor = false;
    custom_compositor = nullptr;
    hide_intersect_reflections = true;
    override_YAxis_height = false;
    new_YAxis_height = 0.0;
    fill_reflection_experimental = false;
    
    enable_reflection_offset = false;
    reflection_offset_position = Vector3(0.0, 0.0, 0.0);
    reflection_offset_rotation = Vector3(0.0, 0.0, 0.0);
    reflection_offset_scale = 1.0;
    offset_blend_mode = 0;
    update_frequency = 1;
    use_lod = false;
    lod_distance_near = 24.0;
    lod_distance_far = 32.0;
    lod_resolution_multiplier = 0.45;
    
    // Initialize internal variables
    frame_counter = 0;
    position_threshold = 0.01;
    rotation_threshold = 0.001;
    is_layer_one_active = true;
    
    // ENHANCED: Performance optimization initialization
    material_cache_valid = false;
    last_viewport_check_frame = -1;
    viewport_check_frequency = 5;
    compositor_effect_initialized = false;
    reflection_plane_cache_valid = false;
    last_distance_check = -1.0;
    cached_lod_factor = 1.0;
}

//Define your destructor (Must have)
PlanarReflectorCPP::~PlanarReflectorCPP() 
{
    // Cleanup is handled by Godot's garbage collection for our node references
}

void PlanarReflectorCPP::_ready() 
{
    // Add to group for easy access from editor plugin
    add_to_group("planar_reflectors");
    // UtilityFunctions::print("[PlanarReflectorCPP]  _ready called, cleaning up viewports");
    clean_viewports();
    
    if (Engine::get_singleton()->is_editor_hint())
    {
        call_deferred("run_editor_setup_init");
    }
    else
    {
        call_deferred("run_game_setup_init");
    }
}

void PlanarReflectorCPP::_notification(int what)
{
    if (what == NOTIFICATION_TRANSFORM_CHANGED) {
        // ENHANCED: Invalidate reflection plane cache when transform changes
        reflection_plane_cache_valid = false;
        if (active_reflect_camera && active_reflect_camera->get_compositor().is_valid()) 
        {
            Ref<Compositor> compositor = active_reflect_camera->get_compositor();
            CompositorEffect *effect = get_reflection_effect(compositor.ptr());
            if (effect) {
                set_reflection_effect(effect);
            }
        }
    }
}

void PlanarReflectorCPP::clean_viewports()
{
    // UtilityFunctions::print("[PlanarReflectorCPP]  Trying to clear Viewport/Cameras");
    game_reflect_viewport = nullptr;
    editor_reflect_viewport = nullptr;
    active_reflect_viewport = nullptr;
}

void PlanarReflectorCPP::run_editor_setup_init()
{
    is_editor_setup_finished = false;
    // Find the editor helper first
    find_editor_helper();

    // Clean up previous editor viewport/camera if they exist
    if (editor_reflect_camera && Object::cast_to<Node>(editor_reflect_camera)) {
        // Clear compositor before freeing camera
        // UtilityFunctions::print("[PlanarReflectorCPP] Trying to free editor_reflect_camera");

        if (editor_reflect_camera->get_compositor().is_valid()) {
            editor_reflect_camera->set_compositor(Ref<Compositor>());
        }
        if (editor_reflect_camera->is_inside_tree() && editor_reflect_camera->get_parent()) {
            editor_reflect_camera->get_parent()->remove_child(editor_reflect_camera);
        }
        if (editor_reflect_camera->is_inside_tree()) {
            editor_reflect_camera->call_deferred("queue_free");
        }
        editor_reflect_camera = nullptr;
        // UtilityFunctions::print("[PlanarReflectorCPP] Freed previous editor_reflect_camera");
    }
    if (editor_reflect_viewport && Object::cast_to<Node>(editor_reflect_viewport)) {

            // UtilityFunctions::print("[PlanarReflectorCPP] Trying to free editor_reflect_viewport");
        if (editor_reflect_viewport->is_inside_tree() && editor_reflect_viewport->get_parent()) {
            editor_reflect_viewport->get_parent()->remove_child(editor_reflect_viewport);
        }
        if (editor_reflect_viewport->is_inside_tree()) {
            editor_reflect_viewport->call_deferred("queue_free");
        }
        editor_reflect_viewport = nullptr;
        // UtilityFunctions::print("[PlanarReflectorCPP] Freed previous editor_reflect_viewport");
    }

    // Create editor-specific viewport and camera
    editor_reflect_viewport = memnew(SubViewport);
    editor_reflect_viewport->set_name("EditorReflectViewport");
    add_child(editor_reflect_viewport);
    // UtilityFunctions::print("[PlanarReflectorCPP] Created editor_reflect_viewport");

    editor_reflect_viewport->set_size(reflection_camera_resolution);
    editor_reflect_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
    editor_reflect_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);
    editor_reflect_viewport->set_positional_shadow_atlas_size(2048);
    editor_reflect_viewport->set_use_own_world_3d(false);
    editor_reflect_viewport->set_transparent_background(true);
    editor_reflect_viewport->set_handle_input_locally(false);

    // Create editor reflection camera
    editor_reflect_camera = memnew(Camera3D);
    editor_reflect_viewport->add_child(editor_reflect_camera);
    // UtilityFunctions::print("[PlanarReflectorCPP] Created editor_reflect_camera");
    editor_reflect_camera->set_cull_mask(reflection_layers);
    editor_reflect_camera->set_current(true);
    
    // Set up a stable environment for editor
    Ref<Environment> editor_env;
    editor_env.instantiate();
    editor_env->set_background(Environment::BG_CLEAR_COLOR);
    editor_env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
    editor_env->set_ambient_light_color(Color(0.8, 0.8, 0.8));
    editor_env->set_ambient_light_energy(1.2);
    editor_reflect_camera->set_environment(editor_env);
    
    // Set active pointers to editor components
    active_main_camera = editor_camera; // May be null initially, will be set by plugin
    active_reflect_camera = editor_reflect_camera;
    active_reflect_viewport = editor_reflect_viewport;

    // Initialize offset cache
    update_offset_cache();
    
    // ENHANCED: Initialize performance caches
    invalidate_all_caches();

    is_editor_setup_finished = true;
    
    if(active_reflect_camera != nullptr)
    {
        setup_compositor_reflection_effect(active_reflect_camera);
    }
    
    // UtilityFunctions::print("PlanarReflectorCPP editor ready completed");
}

void PlanarReflectorCPP::run_game_setup_init()
{
    // For game, use the assigned main camera
    if (!game_main_camera) {
        // UtilityFunctions::print("Warning: No main camera assigned for PlanarReflectorCPP");
        return;
    }
    
    // Set active pointers to game components
    active_main_camera = game_main_camera;
    active_reflect_camera = game_reflect_camera;
    active_reflect_viewport = game_reflect_viewport;

    // Setup for the Running Game
    setup_reflection_viewport();
    setup_reflection_camera();
    setup_reflection_layers();
    setup_reflection_environment();
    
    // Initialize offset cache
    update_offset_cache();
    
    // ENHANCED: Initialize performance caches
    invalidate_all_caches();
    
    // UtilityFunctions::print("PlanarReflectorCPP game ready completed");
}

#pragma region // Editor Functions
void PlanarReflectorCPP::find_editor_helper() {
    if (!Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    
    // Try to find the editor helper singleton
    if (Engine::get_singleton()->has_singleton("PlanarReflectorEditorHelper")) {
        editor_helper = Engine::get_singleton()->get_singleton("PlanarReflectorEditorHelper");
        if (editor_helper) {
            // UtilityFunctions::print("PlanarReflectorCPP: Found editor helper");
        }
    }

    if(active_reflect_camera != nullptr)
    {
        setup_compositor_reflection_effect(active_reflect_camera);
    }
    
}

Camera3D* PlanarReflectorCPP::get_active_camera() {
    return active_main_camera;
}

Viewport* PlanarReflectorCPP::get_active_viewport() {
    if (Engine::get_singleton()->is_editor_hint() && active_main_camera) {
        return active_main_camera->get_viewport();
    }
    return get_viewport();
}
#pragma endregion

void PlanarReflectorCPP::_process(double delta) 
{
    if (Engine::get_singleton()->is_editor_hint() && !is_editor_setup_finished) return;

    if (!is_active) return;

    // Check if we have valid active components
    if (!active_main_camera || !active_reflect_camera || !active_reflect_viewport) {
        return;
    }

    frame_counter++;
    
    // Update offset cache if needed
    update_offset_cache();
    
    // ENHANCED: Less frequent viewport size updates with safety
    if (viewport_check_frequency > 0 && frame_counter % viewport_check_frequency == 0) {
        update_viewport();
    }
    
    // Skip updates based on frequency
    bool should_update = (frame_counter % update_frequency == 0);
    
    if (should_update) {
        if (should_update_reflection()) {
            update_viewport();
            update_reflection_camera();
        }
    }
}

void PlanarReflectorCPP::setup_reflection_viewport()
{
    // This is called for game mode only
    // Clean up previous game viewport if it exists
    if (game_reflect_viewport && Object::cast_to<Node>(game_reflect_viewport)) {
        if (game_reflect_viewport->is_inside_tree() && game_reflect_viewport->get_parent()) {
            game_reflect_viewport->get_parent()->remove_child(game_reflect_viewport);
        }
        if (game_reflect_viewport->is_inside_tree()) {
            game_reflect_viewport->call_deferred("queue_free");
        }
        game_reflect_viewport = nullptr;
        // UtilityFunctions::print("[PlanarReflectorCPP] Freed previous game_reflect_viewport");
    }

    // Create new game viewport
    game_reflect_viewport = memnew(SubViewport);
    game_reflect_viewport->set_name("GameReflectViewport");
    add_child(game_reflect_viewport);
    // UtilityFunctions::print("[PlanarReflectorCPP] Created game_reflect_viewport");

    game_reflect_viewport->set_size(reflection_camera_resolution);
    game_reflect_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
    game_reflect_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);
    game_reflect_viewport->set_positional_shadow_atlas_size(2048);
    game_reflect_viewport->set_use_own_world_3d(false);
    game_reflect_viewport->set_transparent_background(true);
    game_reflect_viewport->set_handle_input_locally(false);

    // Update active pointer
    active_reflect_viewport = game_reflect_viewport;
}

void PlanarReflectorCPP::setup_reflection_camera()
{
    if (!active_reflect_viewport) {
        return;
    }

    // Clean up previous game camera if it exists
    if (game_reflect_camera && Object::cast_to<Node>(game_reflect_camera)) {
        // Clear compositor before freeing camera
        if (game_reflect_camera->get_compositor().is_valid()) {
            game_reflect_camera->set_compositor(Ref<Compositor>());
        }
        if (game_reflect_camera->is_inside_tree() && game_reflect_camera->get_parent()) {
            game_reflect_camera->get_parent()->remove_child(game_reflect_camera);
        }
        if (game_reflect_camera->is_inside_tree()) {
            game_reflect_camera->call_deferred("queue_free");
        }
        game_reflect_camera = nullptr;
        // UtilityFunctions::print("[PlanarReflectorCPP] Freed previous game_reflect_camera");
    }

    // Create new game reflection camera
    game_reflect_camera = memnew(Camera3D);
    active_reflect_viewport->add_child(game_reflect_camera);
    // UtilityFunctions::print("[PlanarReflectorCPP] Created game_reflect_camera");

    if (game_main_camera) {
        // Copy basic camera properties from main camera
        game_reflect_camera->set_attributes(game_main_camera->get_attributes());
        game_reflect_camera->set_doppler_tracking(game_main_camera->get_doppler_tracking());
    }

    game_reflect_camera->set_current(true);

    // Update active pointer
    active_reflect_camera = game_reflect_camera;
}

void PlanarReflectorCPP::setup_reflection_layers()
{
    if (!active_reflect_camera) {
        return;
    }
    
    // Configure which layers the reflection camera can see
    active_reflect_camera->set_cull_mask(reflection_layers);
    
    // Check if layer 1 is active (bit 0 = layer 1)
    if (reflection_layers & (1 << 0)) {
        is_layer_one_active = true;
    } else {
        is_layer_one_active = false;
        UtilityFunctions::print("[PlanarReflectorCPP] Layer 1 not active, make sure to add the layers to the scene Lights cull masks");
    }
}

void PlanarReflectorCPP::setup_reflection_environment()
{
    if (!active_reflect_camera) {
        return;
    }
    
    // Set up environment for reflection camera
    if (use_custom_environment) {
        if (custom_environment) {
            active_reflect_camera->set_environment(custom_environment);
        } else {
            // Auto-generate clean environment
            Ref<Environment> reflection_env;
            reflection_env.instantiate();
            reflection_env->set_background(Environment::BG_CLEAR_COLOR);
            reflection_env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
            reflection_env->set_ambient_light_color(Color(0.8, 0.8, 0.8));
            reflection_env->set_ambient_light_energy(1.2);
            reflection_env->set_fog_enabled(false);
            
            active_reflect_camera->set_environment(reflection_env);
        }
    } else if (active_main_camera) {
        // Use main camera environment
        active_reflect_camera->set_environment(active_main_camera->get_environment());
    }
}

// NEW: C++ COMPOSITOR EFFECTS INTEGRATION
void PlanarReflectorCPP::setup_compositor_reflection_effect(Camera3D *reflect_cam)
{
    //  UtilityFunctions::print(" [PlanarReflectorCPP] setup_compositor_reflection_effect called");
    if (!reflect_cam) {
        return;
    }

    if (use_custom_compositor && custom_compositor) {
        reflect_cam->set_compositor(custom_compositor);
        Ref<Compositor> compositor = reflect_cam->get_compositor();
        if (compositor.is_valid()) {
            TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
            if (effects.size() > 0) {
                CompositorEffect *active_effect = Object::cast_to<CompositorEffect>(effects[0]);
                if (Object::cast_to<ReflectionEffectPrePass>(active_effect)) {
                    set_reflection_effect(active_effect);
                }
            }
        }
    } else {
        Ref<Compositor> current_compositor = reflect_cam->get_compositor();
        if (!current_compositor.is_valid()) {
            create_new_compositor_effect(reflect_cam);
        } else {
            TypedArray<CompositorEffect> effects = current_compositor->get_compositor_effects();
            if (effects.size() > 0) {
                CompositorEffect *active_effect = Object::cast_to<CompositorEffect>(effects[0]);
                if (Object::cast_to<ReflectionEffectPrePass>(active_effect)) {
                    set_reflection_effect(active_effect);
                }
            }
        }
    }

}

void PlanarReflectorCPP::create_new_compositor_effect(Camera3D *reflect_cam)
{
    //  UtilityFunctions::print(" [PlanarReflectorCPP] create_new_compositor_effect called");
    if (!reflect_cam) {
        return;
    }
    
    // Clear existing compositor first
    Ref<Compositor> current_compositor = reflect_cam->get_compositor();
    if (current_compositor.is_valid()) {
        clear_compositor_reflection_effect(reflect_cam);
    }
    
    // Create new compositor
    Ref<Compositor> compositor;
    compositor.instantiate();
    
    // Create new ReflectionEffectPrePass
    Ref<ReflectionEffectPrePass> prepass_effect;
    prepass_effect.instantiate();
    
    // Configure the effect BEFORE adding to compositor
    if (prepass_effect.is_valid()) {
        double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
        prepass_effect->set_intersect_height(height);
        prepass_effect->set_effect_enabled(true);
        prepass_effect->set_fill_enabled(fill_reflection_experimental);
        
        // Create effects array and add configured effect
        TypedArray<ReflectionEffectPrePass> effects;
        effects.push_back(prepass_effect);
        
        // Set effects to compositor
        compositor->set_compositor_effects(effects);
        
        // Finally assign compositor to camera
        reflect_cam->set_compositor(compositor);
    }
    
    compositor_effect_initialized = true;
}

ReflectionEffectPrePass* PlanarReflectorCPP::set_reflection_effect(CompositorEffect *comp_effect)
{
    // UtilityFunctions::print(" [PlanarReflectorCPP] set_reflection_effect called");

    ReflectionEffectPrePass *reflection_effect = Object::cast_to<ReflectionEffectPrePass>(comp_effect);
    if (Object::cast_to<ReflectionEffectPrePass>(reflection_effect)) 
    {
        double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
        reflection_effect->set_intersect_height(height);
        reflection_effect->set_effect_enabled(true);
        reflection_effect->set_fill_enabled(fill_reflection_experimental);
        return reflection_effect;
    }

    // UtilityFunctions::print(" [PlanarReflectorCPP] set_reflection_effect trying to null cast");
    return nullptr;
}

void PlanarReflectorCPP::clear_compositor_reflection_effect(Camera3D *reflect_cam)
{
    // UtilityFunctions::print("Called clear_compositor_reflection_effect");
    Ref<Compositor> compositor = reflect_cam->get_compositor();
    if (compositor.is_valid()) {
        
        // UtilityFunctions::print("CPP Trying to clear compositor effects via clear_compositor_reflection_effect");
        TypedArray<CompositorEffect> active_effects = compositor->get_compositor_effects();
        if(active_effects.size() > 0) {
            active_effects.clear();
            reflect_cam->set_compositor(Ref<Compositor>());
        }


        

    //     // TypedArray<CompositorEffect> empty_effects;
    //     // compositor->set_compositor_effects(empty_effects);
    //     // Ref<Compositor> null_compositor;
    //     // reflect_cam->set_compositor(null_compositor);
    }
}

CompositorEffect* PlanarReflectorCPP::get_reflection_effect(Compositor *comp)
{
    if (!comp) {
        return nullptr;
        // UtilityFunctions::print(" [PlanarReflectorCPP] get_reflection_effect = null 1");

    }
    
    TypedArray<CompositorEffect> effects = comp->get_compositor_effects();
    for (int i = 0; i < effects.size(); i++) {
        CompositorEffect *effect = Object::cast_to<CompositorEffect>(effects[i]);
        if (Object::cast_to<ReflectionEffectPrePass>(effect)) {
            return effect;
        }
    }
    // UtilityFunctions::print(" [PlanarReflectorCPP] get_reflection_effect = null 2");
    return nullptr;
}

Plane PlanarReflectorCPP::calculate_reflection_plane()
{
    // ENHANCED: Cache reflection plane calculations when transform unchanged
    Transform3D current_transform = get_global_transform();
    if (reflection_plane_cache_valid && current_transform.is_equal_approx(last_global_transform)) {
        return cached_reflection_plane;
    }
    
    // Calculate the reflection plane with optional offset and perturbation
    Transform3D reflection_transform = current_transform * Transform3D().rotated(Vector3(1, 0, 0), Math_PI / 2.0);
    Vector3 plane_origin = reflection_transform.get_origin();
    Vector3 plane_normal = reflection_transform.get_basis().get_column(2).normalized();
    
    cached_reflection_plane = Plane(plane_normal, plane_origin.dot(plane_normal));
    last_global_transform = current_transform;
    reflection_plane_cache_valid = true;
    
    return cached_reflection_plane;
}

void PlanarReflectorCPP::update_reflection_camera()
{
    if (!active_main_camera || !active_reflect_camera) {
        return;
    }

    // Update camera projection based on main camera
    update_camera_projection();

    // Calculate reflection plane 
    Plane reflection_plane = calculate_reflection_plane();
    cached_reflection_plane = reflection_plane;
    
    Vector3 cam_pos = active_main_camera->get_global_transform().get_origin();
    
    Vector3 proj_pos = reflection_plane.project(cam_pos);
    Vector3 mirrored_pos = cam_pos + (proj_pos - cam_pos) * 2.0;
    
    // Create base reflection transform
    Transform3D base_reflection_transform;
    base_reflection_transform.set_origin(mirrored_pos);
    
    Basis main_basis = active_main_camera->get_global_transform().get_basis();
    Vector3 normal = reflection_plane.get_normal();
    
    Basis reflection_basis;
    reflection_basis.set_column(0, main_basis.get_column(0).normalized().bounce(normal).normalized());
    reflection_basis.set_column(1, main_basis.get_column(1).normalized().bounce(normal).normalized());
    reflection_basis.set_column(2, main_basis.get_column(2).normalized().bounce(normal).normalized());
    
    base_reflection_transform.set_basis(reflection_basis);
    
    // Apply reflection offset
    Transform3D final_reflection_transform = apply_reflection_offset(base_reflection_transform);
    
    // Set the final transform
    active_reflect_camera->set_global_transform(final_reflection_transform);
    
    // Pass parameters to shader
    call_deferred("update_shader_parameters");
}

void PlanarReflectorCPP::update_camera_projection()
{
    if (!active_main_camera || !active_reflect_camera) {
        return;
    }

    if (auto_detect_camera_mode) {
        active_reflect_camera->set_projection(active_main_camera->get_projection());
        if (Engine::get_singleton()->is_editor_hint()) { // Force perspective in editor
            active_reflect_camera->set_projection(Camera3D::PROJECTION_PERSPECTIVE);
        }
    }
    
    if (active_reflect_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL) {
        active_reflect_camera->set_size(active_main_camera->get_size() * ortho_scale_multiplier);
    } else {
        active_reflect_camera->set_fov(active_main_camera->get_fov());
    }
}

void PlanarReflectorCPP::update_viewport()
{
    if (!active_reflect_viewport) {
        return;
    }

    Vector2i target_size = get_target_viewport_size();
    
    // Apply LOD based on distance
    if (use_lod && active_main_camera) 
    {
        target_size = apply_lod_to_size(target_size, active_main_camera);
    }
    
    // ENHANCED: Only update if size actually changed
    if (cached_viewport_size != target_size) {
        active_reflect_viewport->set_size(target_size);
        cached_viewport_size = target_size;
    }
}

void PlanarReflectorCPP::update_shader_parameters()
{
    if (!active_reflect_viewport) {
        return;
    }

    // ENHANCED: Enhanced material caching with proper invalidation
    if (!is_material_cache_valid()) {
        refresh_material_cache();
    }
    
    ShaderMaterial *material = get_cached_material();
    if (material == nullptr) {
        // No material available - this is normal if no mesh/material assigned yet
        return;
    }
    
    // ENHANCED: Batch shader parameter updates with change detection
    Ref<Texture2D> reflection_texture = active_reflect_viewport->get_texture();
    bool is_orthogonal = false;
    if (Engine::get_singleton()->is_editor_hint()) {
        is_orthogonal = (active_reflect_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL);
    } else if (active_main_camera) {
        is_orthogonal = (active_main_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL);
    }
    
    // Prepare all parameters in a dictionary for batch comparison
    Dictionary new_params;
    new_params["reflection_screen_texture"] = reflection_texture;
    new_params["is_orthogonal_camera"] = is_orthogonal;
    new_params["ortho_uv_scale"] = ortho_uv_scale;
    new_params["reflection_offset_enabled"] = enable_reflection_offset;
    new_params["reflection_offset_position"] = reflection_offset_position;
    new_params["reflection_offset_scale"] = reflection_offset_scale;
    new_params["reflection_plane_normal"] = cached_reflection_plane.get_normal();
    new_params["reflection_plane_distance"] = cached_reflection_plane.d;
    new_params["planar_surface_y"] = get_global_transform().get_origin().y;
    
    // Only update parameters that have changed
    Array param_names = new_params.keys();
    for (int i = 0; i < param_names.size(); i++) {
        String param_name = param_names[i];
        Variant new_value = new_params[param_name];
        if (!cached_shader_params.has(param_name) || !values_equal(cached_shader_params[param_name], new_value)) {
            material->set_shader_parameter(param_name, new_value);
            cached_shader_params[param_name] = new_value;
        }
    }
}

Transform3D PlanarReflectorCPP::apply_reflection_offset(const Transform3D &base_transform)
{
    // Apply reflection offset to the base reflection transform
    if (!enable_reflection_offset) {
        return base_transform;
    }
    
    Transform3D result_transform = base_transform;
    
    switch (offset_blend_mode) {
        case 0: // Add mode - simple addition of offset
            result_transform.set_origin(result_transform.get_origin() + cached_offset_transform.get_origin());
            // Apply rotational offset
            if (reflection_offset_rotation != Vector3()) {
                result_transform.set_basis(result_transform.get_basis() * cached_offset_transform.get_basis());
            }
            break;
        
        case 1: // Multiply mode - relative to current transform
            result_transform = result_transform * cached_offset_transform;
            break;
        
        case 2: // Screen space shift mode - offset relative to camera view
            if (active_main_camera) {
                Vector3 view_offset = active_main_camera->get_global_transform().get_basis().xform(cached_offset_transform.get_origin());
                result_transform.set_origin(result_transform.get_origin() + view_offset);
                result_transform.set_basis(result_transform.get_basis() * cached_offset_transform.get_basis());
            }
            break;
    }
    
    return result_transform;
}

void PlanarReflectorCPP::update_offset_cache()
{
    // Update the cached offset transform when offset parameters change
    if (!enable_reflection_offset) {
        cached_offset_transform = Transform3D();
        return;
    }
    
    // ENHANCED: Only recalculate if values actually changed
    if (last_offset_position.is_equal_approx(reflection_offset_position) && 
        last_offset_rotation.is_equal_approx(reflection_offset_rotation)) {
        return;
    }
    
    // Create offset transform
    Basis offset_basis;
    offset_basis = offset_basis.rotated(Vector3(1, 0, 0), Math::deg_to_rad(reflection_offset_rotation.x));
    offset_basis = offset_basis.rotated(Vector3(0, 1, 0), Math::deg_to_rad(reflection_offset_rotation.y));
    offset_basis = offset_basis.rotated(Vector3(0, 0, 1), Math::deg_to_rad(reflection_offset_rotation.z));
    
    cached_offset_transform = Transform3D(offset_basis, reflection_offset_position * reflection_offset_scale);
    
    last_offset_position = reflection_offset_position;
    last_offset_rotation = reflection_offset_rotation;
}

bool PlanarReflectorCPP::should_update_reflection()
{
    if (!active_main_camera) {
        return false;
    }

    Vector3 current_pos = active_main_camera->get_global_transform().get_origin();
    Basis current_basis = active_main_camera->get_global_transform().get_basis();
    
    // ENHANCED: Check if camera moved/rotated enough to warrant update
    if (last_camera_position != Vector3()) {
        if (current_pos.is_equal_approx(last_camera_position)) {
            Vector3 current_euler = current_basis.get_euler();
            Vector3 last_euler = last_camera_rotation.get_euler();
            if (current_euler.is_equal_approx(last_euler)) {
                return false; // Skip update if camera barely moved
            }
        }
    }
    
    last_camera_position = current_pos;
    last_camera_rotation = current_basis;
    
    return true;
}

// ENHANCED PERFORMANCE HELPER METHODS FROM GDSCRIPT
bool PlanarReflectorCPP::is_material_cache_valid()
{
    // UtilityFunctions::print(" [PlanarReflectorCPP] is_material_cache_valid called");
    if (!material_cache_valid) {
        return false;
    }
    
    // Validate that the material still exists and matches
    Ref<Material> current_material = get_active_material(0);
    if (!current_material.is_valid() || !Object::cast_to<ShaderMaterial>(current_material.ptr())) {
        return false;
    }
    
    return true;
}

void PlanarReflectorCPP::refresh_material_cache()
{
    // UtilityFunctions::print(" [PlanarReflectorCPP] refresh_material_cache called");
    // Check if we have any materials first to prevent surface index errors
    if (get_surface_override_material_count() == 0) {
        active_shader_material = nullptr;
        material_cache_valid = false;
        return;
    }
    
    Ref<Material> material = get_active_material(0);
    if (material.is_valid() && Object::cast_to<ShaderMaterial>(material.ptr())) {
        active_shader_material = Object::cast_to<ShaderMaterial>(material.ptr());
        material_cache_valid = true;
    } else {
        active_shader_material = nullptr;
        material_cache_valid = false;
    }
}

ShaderMaterial* PlanarReflectorCPP::get_cached_material()
{
    if (is_material_cache_valid()) {
        return active_shader_material;
    }
    return nullptr;
}

bool PlanarReflectorCPP::values_equal(Variant a, Variant b)
{
    if (a == b) {
        return true;
    }
    
    // Handle Vector3 comparison with tolerance
    if (a.get_type() == Variant::VECTOR3 && b.get_type() == Variant::VECTOR3) {
        Vector3 vec_a = a;
        Vector3 vec_b = b;
        return vec_a.is_equal_approx(vec_b);
    }
    
    // Handle float comparison with tolerance
    if (a.get_type() == Variant::FLOAT && b.get_type() == Variant::FLOAT) {
        double float_a = a;
        double float_b = b;
        return Math::is_equal_approx(float_a, float_b);
    }
    
    return false;
}

Vector2i PlanarReflectorCPP::get_target_viewport_size()
{
    Vector2i target_size;
    if (Engine::get_singleton()->is_editor_hint() && editor_helper) {
        // Try to get editor viewport size from helper
        Variant size_var = editor_helper->call("get_editor_viewport_size");
        if (size_var.get_type() == Variant::VECTOR2I) {
            target_size = size_var;
        } else {
            // Fallback to active camera's viewport
            Viewport *vp = get_active_viewport();
            if (vp) {
                target_size = vp->get_visible_rect().size;
            } else {
                target_size = Vector2i(1920, 1080); // Default fallback
            }
        }
    } else {
        // Game mode - use regular viewport
        target_size = get_viewport()->get_visible_rect().size;
    }
    return target_size;
}

Vector2i PlanarReflectorCPP::apply_lod_to_size(Vector2i target_size, Camera3D *active_cam)
{
    double distance = get_global_transform().get_origin().distance_to(active_cam->get_global_transform().get_origin());
    
    // ENHANCED: Cache LOD calculations when distance hasn't changed much
    if (Math::abs(distance - last_distance_check) > 1.0) {
        double lod_factor = 1.0;
        if (distance > lod_distance_near) {
            double lerp_factor = Math::clamp((distance - lod_distance_near) / (lod_distance_far - lod_distance_near), 0.0, 1.0);
            lod_factor = Math::lerp(1.0, lod_resolution_multiplier, lerp_factor);
        }
        cached_lod_factor = lod_factor;
        last_distance_check = distance;
    }
    
    Vector2i result_size = Vector2i((double)target_size.x * cached_lod_factor, (double)target_size.y * cached_lod_factor);
    result_size.x = Math::max(result_size.x, 128); // Minimum resolution
    result_size.y = Math::max(result_size.y, 128);
    return result_size;
}

void PlanarReflectorCPP::invalidate_all_caches()
{
    // UtilityFunctions::print("[PlanarReflectorCPP] invalidate_all_caches called, cleaning up nodes");
    material_cache_valid = false;
    active_shader_material = nullptr;
    cached_shader_params.clear();
    reflection_plane_cache_valid = false;
    last_viewport_check_frame = -1;
    cached_viewport_size = Vector2i(0, 0);
    compositor_effect_initialized = false;
    last_compositor_settings.clear();
}

void PlanarReflectorCPP::_exit_tree() 
{
    // UtilityFunctions::print("[PlanarReflectorCPP] _exit_tree called, cleaning up nodes");

    // Safely clear compositor effects before freeing nodes
    if (active_reflect_camera) {
        clear_compositor_reflection_effect(active_reflect_camera);
    }

    // Remove and free game components
    if (game_reflect_camera && Object::cast_to<Node>(game_reflect_camera)) {
        // Clear compositor before freeing camera
        if (game_reflect_camera->get_compositor().is_valid()) {
            game_reflect_camera->set_compositor(Ref<Compositor>());
        }
        if (game_reflect_camera->is_inside_tree() && game_reflect_camera->get_parent()) {
            game_reflect_camera->get_parent()->remove_child(game_reflect_camera);
        }
        if (game_reflect_camera->is_inside_tree()) {
            game_reflect_camera->call_deferred("queue_free");
            // UtilityFunctions::print("[PlanarReflectorCPP] Freed game_reflect_camera");
        }
        game_reflect_camera = nullptr;
    }
    if (game_reflect_viewport && Object::cast_to<Node>(game_reflect_viewport)) {
        if (game_reflect_viewport->is_inside_tree() && game_reflect_viewport->get_parent()) {
            game_reflect_viewport->get_parent()->remove_child(game_reflect_viewport);
        }
        if (game_reflect_viewport->is_inside_tree()) {
            game_reflect_viewport->call_deferred("queue_free");
            // UtilityFunctions::print("[PlanarReflectorCPP] Freed game_reflect_viewport");
        }
        game_reflect_viewport = nullptr;
    }

    // Remove and free editor components
    if (editor_reflect_camera && Object::cast_to<Node>(editor_reflect_camera)) {
        // Clear compositor before freeing camera
        if (editor_reflect_camera->get_compositor().is_valid()) {
            editor_reflect_camera->set_compositor(Ref<Compositor>());
        }
        if (editor_reflect_camera->is_inside_tree() && editor_reflect_camera->get_parent()) {
            editor_reflect_camera->get_parent()->remove_child(editor_reflect_camera);
        }
        if (editor_reflect_camera->is_inside_tree()) {
            editor_reflect_camera->call_deferred("queue_free");
            // UtilityFunctions::print("[PlanarReflectorCPP] Freed editor_reflect_camera");
        }
        editor_reflect_camera = nullptr;
    }
    if (editor_reflect_viewport && Object::cast_to<Node>(editor_reflect_viewport)) {
        if (editor_reflect_viewport->is_inside_tree() && editor_reflect_viewport->get_parent()) {
            editor_reflect_viewport->get_parent()->remove_child(editor_reflect_viewport);
        }
        if (editor_reflect_viewport->is_inside_tree()) {
            editor_reflect_viewport->call_deferred("queue_free");
            // UtilityFunctions::print("[PlanarReflectorCPP] Freed editor_reflect_viewport");
        }
        editor_reflect_viewport = nullptr;
    }

    // Clear active pointers
    active_main_camera = nullptr;
    active_reflect_camera = nullptr;
    active_reflect_viewport = nullptr;

    // Invalidate caches and references
    invalidate_all_caches();
    editor_helper = nullptr;
    custom_compositor = nullptr;
    custom_environment = nullptr;
}

//METHOD BINDINGS AND SETTERS FROM HERE
void PlanarReflectorCPP::_bind_methods() 
{
    //Method bindings to be able to access those from GDScript and make Godot "See those"
    ClassDB::bind_method(D_METHOD("set_is_active", "p_active"), &PlanarReflectorCPP::set_is_active);
    ClassDB::bind_method(D_METHOD("get_is_active"), &PlanarReflectorCPP::get_is_active);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_active"), "set_is_active", "get_is_active");

    // Core camera and resolution properties
    ClassDB::bind_method(D_METHOD("set_game_main_camera", "p_camera"), &PlanarReflectorCPP::set_game_main_camera);
    ClassDB::bind_method(D_METHOD("get_game_main_camera"), &PlanarReflectorCPP::get_game_main_camera);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "main_camera", PROPERTY_HINT_NODE_TYPE, "Camera3D"), "set_game_main_camera", "get_game_main_camera");
    
    // Editor camera methods (not exposed as property)
    ClassDB::bind_method(D_METHOD("set_editor_camera", "p_camera"), &PlanarReflectorCPP::set_editor_camera);
    ClassDB::bind_method(D_METHOD("get_editor_camera"), &PlanarReflectorCPP::get_editor_camera);
    
    ClassDB::bind_method(D_METHOD("set_reflection_camera_resolution", "p_resolution"), &PlanarReflectorCPP::set_reflection_camera_resolution);
    ClassDB::bind_method(D_METHOD("get_reflection_camera_resolution"), &PlanarReflectorCPP::get_reflection_camera_resolution);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "reflection_camera_resolution"), "set_reflection_camera_resolution", "get_reflection_camera_resolution");

    // Camera Controls Group
    ADD_GROUP("Camera Controls", "");
    ClassDB::bind_method(D_METHOD("set_ortho_scale_multiplier", "p_multiplier"), &PlanarReflectorCPP::set_ortho_scale_multiplier);
    ClassDB::bind_method(D_METHOD("get_ortho_scale_multiplier"), &PlanarReflectorCPP::get_ortho_scale_multiplier);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ortho_scale_multiplier"), "set_ortho_scale_multiplier", "get_ortho_scale_multiplier");

    ClassDB::bind_method(D_METHOD("set_ortho_uv_scale", "p_scale"), &PlanarReflectorCPP::set_ortho_uv_scale);
    ClassDB::bind_method(D_METHOD("get_ortho_uv_scale"), &PlanarReflectorCPP::get_ortho_uv_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ortho_uv_scale"), "set_ortho_uv_scale", "get_ortho_uv_scale");
    
    ClassDB::bind_method(D_METHOD("set_auto_detect_camera_mode", "p_auto_detect"), &PlanarReflectorCPP::set_auto_detect_camera_mode);
    ClassDB::bind_method(D_METHOD("get_auto_detect_camera_mode"), &PlanarReflectorCPP::get_auto_detect_camera_mode);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_detect_camera_mode"), "set_auto_detect_camera_mode", "get_auto_detect_camera_mode");

    // Reflection Layers and Environment Group
    ADD_GROUP("Reflection Layers and Environment", "");
    ClassDB::bind_method(D_METHOD("set_reflection_layers", "p_layers"), &PlanarReflectorCPP::set_reflection_layers);
    ClassDB::bind_method(D_METHOD("get_reflection_layers"), &PlanarReflectorCPP::get_reflection_layers);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_reflection_layers", "get_reflection_layers");

    ClassDB::bind_method(D_METHOD("set_use_custom_environment", "p_use_custom"), &PlanarReflectorCPP::set_use_custom_environment);
    ClassDB::bind_method(D_METHOD("get_use_custom_environment"), &PlanarReflectorCPP::get_use_custom_environment);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_custom_environment"), "set_use_custom_environment", "get_use_custom_environment");

    ClassDB::bind_method(D_METHOD("set_custom_environment", "p_environment"), &PlanarReflectorCPP::set_custom_environment);
    ClassDB::bind_method(D_METHOD("get_custom_environment"), &PlanarReflectorCPP::get_custom_environment);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "custom_environment", PROPERTY_HINT_RESOURCE_TYPE, "Environment"), "set_custom_environment", "get_custom_environment");

    // NEW: Reflection Compositor Effects Group
    ADD_GROUP("Reflection Compositor Effects", "");
    ClassDB::bind_method(D_METHOD("set_use_custom_compositor", "p_use_custom"), &PlanarReflectorCPP::set_use_custom_compositor);
    ClassDB::bind_method(D_METHOD("get_use_custom_compositor"), &PlanarReflectorCPP::get_use_custom_compositor);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_custom_compositor"), "set_use_custom_compositor", "get_use_custom_compositor");

    ClassDB::bind_method(D_METHOD("set_custom_compositor", "p_compositor"), &PlanarReflectorCPP::set_custom_compositor);
    ClassDB::bind_method(D_METHOD("get_custom_compositor"), &PlanarReflectorCPP::get_custom_compositor);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "custom_compositor", PROPERTY_HINT_RESOURCE_TYPE, "Compositor"), "set_custom_compositor", "get_custom_compositor");

    ClassDB::bind_method(D_METHOD("set_hide_intersect_reflections", "p_hide"), &PlanarReflectorCPP::set_hide_intersect_reflections);
    ClassDB::bind_method(D_METHOD("get_hide_intersect_reflections"), &PlanarReflectorCPP::get_hide_intersect_reflections);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hide_intersect_reflections"), "set_hide_intersect_reflections", "get_hide_intersect_reflections");

    ClassDB::bind_method(D_METHOD("set_override_YAxis_height", "p_override"), &PlanarReflectorCPP::set_override_YAxis_height);
    ClassDB::bind_method(D_METHOD("get_override_YAxis_height"), &PlanarReflectorCPP::get_override_YAxis_height);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "override_YAxis_height"), "set_override_YAxis_height", "get_override_YAxis_height");

    ClassDB::bind_method(D_METHOD("set_new_YAxis_height", "p_height"), &PlanarReflectorCPP::set_new_YAxis_height);
    ClassDB::bind_method(D_METHOD("get_new_YAxis_height"), &PlanarReflectorCPP::get_new_YAxis_height);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "new_YAxis_height"), "set_new_YAxis_height", "get_new_YAxis_height");

    ClassDB::bind_method(D_METHOD("set_fill_reflection_experimental", "p_fill"), &PlanarReflectorCPP::set_fill_reflection_experimental);
    ClassDB::bind_method(D_METHOD("get_fill_reflection_experimental"), &PlanarReflectorCPP::get_fill_reflection_experimental);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fill_reflection_experimental"), "set_fill_reflection_experimental", "get_fill_reflection_experimental");

    // Reflection Offset Control Group
    ADD_GROUP("Reflection Offset Control", "");
    ClassDB::bind_method(D_METHOD("set_enable_reflection_offset", "p_enable"), &PlanarReflectorCPP::set_enable_reflection_offset);
    ClassDB::bind_method(D_METHOD("get_enable_reflection_offset"), &PlanarReflectorCPP::get_enable_reflection_offset);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_reflection_offset"), "set_enable_reflection_offset", "get_enable_reflection_offset");

    ClassDB::bind_method(D_METHOD("set_reflection_offset_position", "p_position"), &PlanarReflectorCPP::set_reflection_offset_position);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_position"), &PlanarReflectorCPP::get_reflection_offset_position);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "reflection_offset_position"), "set_reflection_offset_position", "get_reflection_offset_position");

    ClassDB::bind_method(D_METHOD("set_reflection_offset_rotation", "p_rotation"), &PlanarReflectorCPP::set_reflection_offset_rotation);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_rotation"), &PlanarReflectorCPP::get_reflection_offset_rotation);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "reflection_offset_rotation"), "set_reflection_offset_rotation", "get_reflection_offset_rotation");

    ClassDB::bind_method(D_METHOD("set_reflection_offset_scale", "p_scale"), &PlanarReflectorCPP::set_reflection_offset_scale);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_scale"), &PlanarReflectorCPP::get_reflection_offset_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_offset_scale"), "set_reflection_offset_scale", "get_reflection_offset_scale");

    ClassDB::bind_method(D_METHOD("set_offset_blend_mode", "p_mode"), &PlanarReflectorCPP::set_offset_blend_mode);
    ClassDB::bind_method(D_METHOD("get_offset_blend_mode"), &PlanarReflectorCPP::get_offset_blend_mode);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "offset_blend_mode", PROPERTY_HINT_ENUM, "Add,Multiply,Screen Space Shift"), "set_offset_blend_mode", "get_offset_blend_mode");

    // Performance Controls Group
    ADD_GROUP("Performance Controls", "");
    ClassDB::bind_method(D_METHOD("set_update_frequency", "p_frequency"), &PlanarReflectorCPP::set_update_frequency);
    ClassDB::bind_method(D_METHOD("get_update_frequency"), &PlanarReflectorCPP::get_update_frequency);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "update_frequency", PROPERTY_HINT_RANGE, "1,10,1"), "set_update_frequency", "get_update_frequency");

    ClassDB::bind_method(D_METHOD("set_use_lod", "p_use_lod"), &PlanarReflectorCPP::set_use_lod);
    ClassDB::bind_method(D_METHOD("get_use_lod"), &PlanarReflectorCPP::get_use_lod);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_lod"), "set_use_lod", "get_use_lod");

    ClassDB::bind_method(D_METHOD("set_lod_distance_near", "p_distance"), &PlanarReflectorCPP::set_lod_distance_near);
    ClassDB::bind_method(D_METHOD("get_lod_distance_near"), &PlanarReflectorCPP::get_lod_distance_near);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance_near"), "set_lod_distance_near", "get_lod_distance_near");

    ClassDB::bind_method(D_METHOD("set_lod_distance_far", "p_distance"), &PlanarReflectorCPP::set_lod_distance_far);
    ClassDB::bind_method(D_METHOD("get_lod_distance_far"), &PlanarReflectorCPP::get_lod_distance_far);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance_far"), "set_lod_distance_far", "get_lod_distance_far");

    ClassDB::bind_method(D_METHOD("set_lod_resolution_multiplier", "p_multiplier"), &PlanarReflectorCPP::set_lod_resolution_multiplier);
    ClassDB::bind_method(D_METHOD("get_lod_resolution_multiplier"), &PlanarReflectorCPP::get_lod_resolution_multiplier);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_resolution_multiplier", PROPERTY_HINT_RANGE, "0.1,1.0,0.01"), "set_lod_resolution_multiplier", "get_lod_resolution_multiplier");

    ClassDB::bind_method(D_METHOD("update_shader_parameters"), &PlanarReflectorCPP::update_shader_parameters);
    ClassDB::bind_method(D_METHOD("update_viewport"), &PlanarReflectorCPP::update_viewport);
    ClassDB::bind_method(D_METHOD("update_reflection_camera"), &PlanarReflectorCPP::update_reflection_camera);
    ClassDB::bind_method(D_METHOD("get_active_camera"), &PlanarReflectorCPP::get_active_camera);
    ClassDB::bind_method(D_METHOD("run_editor_setup_init"), &PlanarReflectorCPP::run_editor_setup_init);
    ClassDB::bind_method(D_METHOD("run_game_setup_init"), &PlanarReflectorCPP::run_game_setup_init);
}

// SETTERS AND GETTERS IMPLEMENTATION FROM HERE:

void PlanarReflectorCPP::set_is_active(const bool p_active) { is_active = p_active; }
bool PlanarReflectorCPP::get_is_active() const { return is_active; }

// Core camera and resolution controls
void PlanarReflectorCPP::set_game_main_camera(Camera3D *p_camera) 
{
    game_main_camera = Object::cast_to<Camera3D>(p_camera);
    
    // If we're in game mode and already initialized, update the active pointer
    if (!Engine::get_singleton()->is_editor_hint() && game_reflect_camera) {
        active_main_camera = game_main_camera;
        
        // Update reflection camera properties
        if (game_main_camera && game_reflect_camera) {
            game_reflect_camera->set_attributes(game_main_camera->get_attributes());
            game_reflect_camera->set_doppler_tracking(game_main_camera->get_doppler_tracking());
            setup_reflection_environment();
        }
    }
}

Camera3D* PlanarReflectorCPP::get_game_main_camera() const { return game_main_camera; }

void PlanarReflectorCPP::set_editor_camera(Camera3D *p_camera) 
{
    editor_camera = Object::cast_to<Camera3D>(p_camera);
    
    if (Engine::get_singleton()->is_editor_hint() && editor_camera) {
        // Update active camera pointer
        active_main_camera = editor_camera;
        
        // Update editor reflection camera properties if it exists
        if (editor_reflect_camera) 
        {
            editor_reflect_camera->set_projection(editor_camera->get_projection());
            if (editor_reflect_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL) {
                editor_reflect_camera->set_size(editor_camera->get_size() * ortho_scale_multiplier);
            } else {
                editor_reflect_camera->set_fov(editor_camera->get_fov());
            }
        }
        
        // Invalidate caches when editor camera changes
        invalidate_all_caches();
        call_deferred("run_editor_setup_init");
        call_deferred("update_viewport");
        call_deferred("update_reflection_camera");
    }
}

Camera3D* PlanarReflectorCPP::get_editor_camera() const { return editor_camera; }

void PlanarReflectorCPP::set_reflection_camera_resolution(const Vector2i p_resolution) 
{ 
    reflection_camera_resolution = p_resolution;
    
    // Update viewport size if it exists
    if (active_reflect_viewport) {
        active_reflect_viewport->set_size(reflection_camera_resolution);
        cached_viewport_size = reflection_camera_resolution; // Update cache
    }
}

Vector2i PlanarReflectorCPP::get_reflection_camera_resolution() const { return reflection_camera_resolution; }

// Camera Controls Group
void PlanarReflectorCPP::set_ortho_scale_multiplier(double p_multiplier) { ortho_scale_multiplier = p_multiplier; }
double PlanarReflectorCPP::get_ortho_scale_multiplier() const { return ortho_scale_multiplier; }

void PlanarReflectorCPP::set_ortho_uv_scale(double p_scale) { ortho_uv_scale = p_scale; }
double PlanarReflectorCPP::get_ortho_uv_scale() const { return ortho_uv_scale; }

void PlanarReflectorCPP::set_auto_detect_camera_mode(bool p_auto_detect) { auto_detect_camera_mode = p_auto_detect; }
bool PlanarReflectorCPP::get_auto_detect_camera_mode() const { return auto_detect_camera_mode; }

// Reflection Layers and Environment Group
void PlanarReflectorCPP::set_reflection_layers(int p_layers)
{
    reflection_layers = p_layers;    
    // Update reflection camera cull mask if it exists
    if (active_reflect_camera) {
        setup_reflection_layers();
    }
}
int PlanarReflectorCPP::get_reflection_layers() const { return reflection_layers; }

void PlanarReflectorCPP::set_use_custom_environment(bool p_use_custom)
{
    use_custom_environment = p_use_custom;
    // Update environment setup if reflection camera exists
    if (active_reflect_camera) {
        setup_reflection_environment();
    }
}

bool PlanarReflectorCPP::get_use_custom_environment() const { return use_custom_environment; }

void PlanarReflectorCPP::set_custom_environment(Environment *p_environment)
{
    custom_environment = Object::cast_to<Environment>(p_environment);
    // Update environment setup if reflection camera exists and we're using custom environment
    if (active_reflect_camera && use_custom_environment) {
        setup_reflection_environment();
    }
}
Environment* PlanarReflectorCPP::get_custom_environment() const { return custom_environment; }

// NEW: Reflection Compositor Effects Group
void PlanarReflectorCPP::set_use_custom_compositor(bool p_use_custom)
{
    use_custom_compositor = p_use_custom;
    if (active_reflect_camera) {
        setup_compositor_reflection_effect(active_reflect_camera);
    }
}
bool PlanarReflectorCPP::get_use_custom_compositor() const { return use_custom_compositor; }

void PlanarReflectorCPP::set_custom_compositor(Compositor *p_compositor)
{
    custom_compositor = Object::cast_to<Compositor>(p_compositor);
    if (active_reflect_camera && use_custom_compositor) {
        setup_compositor_reflection_effect(active_reflect_camera);
    }
}
Compositor* PlanarReflectorCPP::get_custom_compositor() const { return custom_compositor; }

void PlanarReflectorCPP::set_hide_intersect_reflections(bool p_hide)
{
    hide_intersect_reflections = p_hide;
    if (active_reflect_camera) {
        if (p_hide) {
            setup_compositor_reflection_effect(active_reflect_camera);
        } else {
            clear_compositor_reflection_effect(active_reflect_camera);
        }
    }
}
bool PlanarReflectorCPP::get_hide_intersect_reflections() const { return hide_intersect_reflections; }

void PlanarReflectorCPP::set_override_YAxis_height(bool p_override)
{
    override_YAxis_height = p_override;
    if (active_reflect_camera) {
        setup_compositor_reflection_effect(active_reflect_camera);
    }
}
bool PlanarReflectorCPP::get_override_YAxis_height() const { return override_YAxis_height; }

void PlanarReflectorCPP::set_new_YAxis_height(double p_height)
{
    new_YAxis_height = p_height;
    if (active_reflect_camera) {
        setup_compositor_reflection_effect(active_reflect_camera);
    }
}
double PlanarReflectorCPP::get_new_YAxis_height() const { return new_YAxis_height; }

void PlanarReflectorCPP::set_fill_reflection_experimental(bool p_fill)
{
    fill_reflection_experimental = p_fill;
    if (active_reflect_camera) {
        setup_compositor_reflection_effect(active_reflect_camera);
    }
}
bool PlanarReflectorCPP::get_fill_reflection_experimental() const { return fill_reflection_experimental; }

// Reflection Offset Control Group
void PlanarReflectorCPP::set_enable_reflection_offset(bool p_enable)
{
    enable_reflection_offset = p_enable;
    update_offset_cache();
}
bool PlanarReflectorCPP::get_enable_reflection_offset() const { return enable_reflection_offset; }

void PlanarReflectorCPP::set_reflection_offset_position(const Vector3 &p_position)
{
    reflection_offset_position = p_position;
    update_offset_cache();
}
Vector3 PlanarReflectorCPP::get_reflection_offset_position() const { return reflection_offset_position; }

void PlanarReflectorCPP::set_reflection_offset_rotation(const Vector3 &p_rotation)
{
    reflection_offset_rotation = p_rotation;
    update_offset_cache();
}
Vector3 PlanarReflectorCPP::get_reflection_offset_rotation() const { return reflection_offset_rotation; }

void PlanarReflectorCPP::set_reflection_offset_scale(double p_scale)
{
    reflection_offset_scale = p_scale;
    update_offset_cache();
}
double PlanarReflectorCPP::get_reflection_offset_scale() const { return reflection_offset_scale; }

void PlanarReflectorCPP::set_offset_blend_mode(int p_mode) { offset_blend_mode = Math::clamp(p_mode, 0, 2); }
int PlanarReflectorCPP::get_offset_blend_mode() const { return offset_blend_mode; }

// Performance Controls Group
void PlanarReflectorCPP::set_update_frequency(int p_frequency) { update_frequency = Math::max(p_frequency, 1); }
int PlanarReflectorCPP::get_update_frequency() const { return update_frequency; }

void PlanarReflectorCPP::set_use_lod(bool p_use_lod) { use_lod = p_use_lod; }
bool PlanarReflectorCPP::get_use_lod() const { return use_lod; }

void PlanarReflectorCPP::set_lod_distance_near(double p_distance) { lod_distance_near = p_distance; }
double PlanarReflectorCPP::get_lod_distance_near() const { return lod_distance_near; }

void PlanarReflectorCPP::set_lod_distance_far(double p_distance) { lod_distance_far = p_distance; }
double PlanarReflectorCPP::get_lod_distance_far() const { return lod_distance_far; }

void PlanarReflectorCPP::set_lod_resolution_multiplier(double p_multiplier) { lod_resolution_multiplier = p_multiplier; }
double PlanarReflectorCPP::get_lod_resolution_multiplier() const { return lod_resolution_multiplier; }