/**
 * @file PlanarReflectorCPP.cpp
 * @brief Implementation of PlanarReflectorCPP - A high-performance C++ GDExtension for real-time planar reflections in Godot 4.4
 * This class creates realistic water and mirror reflections with PlanarReflector 
 * @author DanTrZ
 * @version 2.0
 * @date 2024
 */

#include "PlanarReflectorCPP.h"

// Core Godot includes for basic functionality
#include <godot_cpp/core/class_db.hpp> 
#include <godot_cpp/godot.hpp> 
#include <godot_cpp/variant/utility_functions.hpp> 
#include <godot_cpp/classes/engine.hpp>

// Scene and rendering includes
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/camera_attributes.hpp>

// Compositor system includes for advanced effects
#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

// Math and utility includes
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

PlanarReflectorCPP::PlanarReflectorCPP() 
{  
    // Core functionality - enable by default for immediate visual feedback
    is_active = true;
    // Can be reduced via LOD system for distant objects
    reflection_camera_resolution = Vector2i(1920, 1080);
    
    // Camera projection settings - auto-detect provides best compatibility
    ortho_scale_multiplier = 1.0;    // 1:1 scale for orthogonal cameras
    ortho_uv_scale = 1.0;            // No UV scaling by default
    auto_detect_camera_mode = true;  // Automatically match main camera projection
    
    // Rendering layers - default to layer 1 (most common setup)
    reflection_layers = 1;
    use_custom_environment = false;  // Use generated environment for consistency
    
    // Compositor Effects - Enable for advanced reflection masking
    hide_intersect_reflections = true;      // Hide geometry intersecting the reflection plane (EXPERIMENTAL Hide underwater objects)
    override_YAxis_height = false;          // Use reflector's Y position by default
    new_YAxis_height = 0.0;                 // Custom height when override is enabled
    fill_reflection_experimental = false;   // Experimental hole-filling disabled by default
    
    // Offset controls - Disabled by default for predictable reflections
    enable_reflection_offset = false;
    reflection_offset_position = Vector3(0.0, 0.0, 0.0);  // No positional offset
    reflection_offset_rotation = Vector3(0.0, 0.0, 0.0);  // No rotational offset
    reflection_offset_scale = 1.0;                         // 1:1 scale
    offset_blend_mode = 0;                                 // Additive blend mode
    
    // Performance settings - Balanced for quality and performance
    update_frequency = 3;               // Update every 3rd frame (20fps at 60fps)
    use_lod = true;                     // Enable distance-based quality reduction
    lod_distance_near = 10.0;           // Full quality within 10 units
    lod_distance_far = 25.0;            // Minimum quality beyond 25 units
    lod_resolution_multiplier = 0.45;   // Reduce to 45% resolution when far
    
    // Internal state initialization
    frame_counter = 0;                  // Tracks frames for update frequency
    position_threshold = 0.01;          // Minimum movement for updates (unused in current implementation)
    rotation_threshold = 0.001;         // Minimum rotation for updates (unused in current implementation)
    is_layer_one_active = true;         // Tracks if layer 1 is in reflection_layers
    
    // Performance optimization caches
    last_viewport_check_frame = -1;     // Last frame viewport size was checked
    viewport_check_frequency = 5;       // Check viewport size every 5 frames
    last_distance_check = -1.0;         // Last distance calculation for LOD
    cached_lod_factor = 1.0;            // Cached LOD multiplication factor
}

PlanarReflectorCPP::~PlanarReflectorCPP() 
{
}

void PlanarReflectorCPP::_ready() 
{
    // Add to group for easy identification by editor plugins and editor helper class that will set the Editor Camera in this class
    add_to_group("planar_reflectors");
    // Clear any existing shader references to prevent texture leaks and shader references breaking
    clear_shader_texture_references();
    // Defer main setup to next frame to ensure scene tree is fully constructed
    call_deferred("initial_setup");
}

/**
 * @brief Handles Godot notifications (transform changes, etc.)
 * Currently responds to NOTIFICATION_TRANSFORM_CHANGED by updating
 * reflection calculations when the reflector moves or rotates.
 * @param what The notification type from Godot
 */
void PlanarReflectorCPP::_notification(int what)
{
    if (what == NOTIFICATION_TRANSFORM_CHANGED) {
        // Only update if we have a valid reflection camera with compositor
        if (reflect_camera && reflect_camera->get_compositor().is_valid()) 
        {
            // Update viewport size in case screen resolution changed
            update_reflect_viewport_size();
            
            // Recalculate reflection camera position and orientation
            set_reflection_camera_transform();
            
            // Update compositor effect parameters (e.g., intersection height)
            update_compositor_parameters();
        }
    }
}

/**
 * @brief Main setup function - Configures reflection system based on context
 * Determines if we're running in editor or game mode and initializes
 * Setup Flow:
 * 1. Find editor helper (if in editor mode)
 * 2. Verify node is in scene tree
 * 3. Create viewport and camera system
 * 4. Defer finalization to next frame
 */
void PlanarReflectorCPP::initial_setup()
{
    // In editor mode, find the singleton helper for viewport size detection
    if (Engine::get_singleton()->is_editor_hint()) {
        find_editor_helper();
    }

    // CRITICAL: Ensure we're properly in the scene tree before creating children
    // This prevents crashes from adding nodes before the tree is ready
    if (!is_inside_tree()) {
        call_deferred("initial_setup");  // Retry next frame
        return;
    }

    // Create the core viewport and camera system
    setup_reflection_camera_and_viewport();
    
    // Defer final setup to ensure viewport is fully initialized
    call_deferred("finalize_setup");
}

/**
 * @brief Completes the setup process after viewport creation
 * This function runs after the viewport and camera are created
 * to perform final initialization that requires the rendering
 * system to be fully ready.
 */
void PlanarReflectorCPP::finalize_setup()
{
    // Set initial viewport size based on current screen/editor size
    update_reflect_viewport_size();
    
    // Calculate and set initial reflection camera transform
    set_reflection_camera_transform();
}

/**
 * @brief Main update loop - Called every frame by Godot
 * Update Schedule:
 * - Viewport size: Every 5th frame (configurable)
 * - Reflection transform: Every Nth frame based on update_frequency
 * @param delta Time elapsed since last frame (unused in current implementation)
 */
void PlanarReflectorCPP::_process(double delta) 
{
    // Early exit if not ready or disabled - saves CPU cycles
    if (!is_inside_tree() || !is_active) {
        return;
    }
     
    frame_counter++;  // Track frames for frequency-based updates
    
    // Periodically check if viewport size needs updating
    if (viewport_check_frequency > 0 && frame_counter % viewport_check_frequency == 0) {
        update_reflect_viewport_size();
    }
    
    // Update reflection camera at configured frequency
    bool should_update = (update_frequency > 0 && frame_counter % update_frequency == 0);

    if (should_update) {
        Camera3D *active_cam = get_active_camera();
        if (active_cam) {
            // Recalculate reflection camera position and update shader parameters
            set_reflection_camera_transform();
        }
    }
}

/**
 * @brief Creates the reflection viewport and camera system
 * 
 * This is the core setup function that creates:
 * 1. SubViewport for rendering reflections
 * 2. Camera3D for capturing the reflected view
 * 3. Proper cleanup of any existing system
 */
void PlanarReflectorCPP::setup_reflection_camera_and_viewport()
{
    // CRITICAL: Clear shader texture references before destroying viewport
    // This prevents crashes from dangling texture pointers in materials
    clear_shader_texture_references();
    
    // Safe cleanup of existing reflection viewport
    if (reflect_viewport) {
        if (reflect_viewport->is_inside_tree()) {
            reflect_viewport->get_parent()->remove_child(reflect_viewport);
        }
        reflect_viewport->queue_free();  // Godot's safe deletion method
        reflect_viewport = nullptr;
    }
    
    // Camera will be freed automatically when viewport is destroyed
    if (reflect_camera) {
        reflect_camera = nullptr;
    }

    // PERFORMANCE: Wait one frame before creating new viewport
    // This prevents rapid create/destroy cycles during initialization
    if (frame_counter > 0) {
        call_deferred("create_viewport_deferred");
        return;
    }
    
    // First frame - create immediately
    create_viewport_deferred();
}

/**
 * @brief Deferred viewport creation - Ensures safe initialization
 */
void PlanarReflectorCPP::create_viewport_deferred()
{
    // Create SubViewport with unique name to avoid conflicts
    reflect_viewport = memnew(SubViewport);
    String unique_name = "ReflectionViewPort";
    reflect_viewport->set_name(unique_name);
    
    // Add as child - this viewport will render our reflection
    add_child(reflect_viewport);
    
    // Configure viewport for reflection rendering
    reflect_viewport->set_size(reflection_camera_resolution);           // Set target resolution
    reflect_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);      // Continuous updates
    reflect_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);             // MSAA off for performance
    reflect_viewport->set_positional_shadow_atlas_size(2048);           // Decent shadow quality
    reflect_viewport->set_use_own_world_3d(false);                      // Share world with main scene
    reflect_viewport->set_transparent_background(true);                 // Allow alpha blending
    reflect_viewport->set_handle_input_locally(false);                  // No input needed

    // Create the reflection camera
    reflect_camera = memnew(Camera3D);
    reflect_camera->set_name("ReflectCamera");
    reflect_viewport->add_child(reflect_camera);
    
    // Configure camera layer visibility
    int cull_mask = reflection_layers;
    reflect_camera->set_cull_mask(cull_mask);
    
    // Check if layer 1 is active (important for lighting)
    is_layer_one_active = bool(cull_mask & (1 << 0));
    
    // Copy properties from main camera if available
    if (main_camera) {
        reflect_camera->set_attributes(main_camera->get_attributes());      // Copy camera attributes
        reflect_camera->set_doppler_tracking(main_camera->get_doppler_tracking());  // Copy audio settings
    }
    
    // Make this camera active for its viewport
    reflect_camera->set_current(true);
    
    // Setup environment and compositor effects
    setup_reflection_environment();
    
    // Setup compositor effects for advanced reflection features
    if (reflect_camera) {
        call_deferred("setup_compositor_reflection_effect", reflect_camera);
    }
}

/**
 * @brief Configures the reflection camera's rendering environment
 * 
 * Sets up the Environment resource that controls:
 * - Background rendering (clear color vs skybox)
 * - Ambient lighting settings
 * - Fog and atmospheric effects
 */
void PlanarReflectorCPP::setup_reflection_environment()
{
    if (!reflect_camera) {
        return;
    }
    
    Ref<Environment> reflection_env;
    
    // Use custom environment if specified
    if (use_custom_environment && custom_environment) {
        reflection_env = Ref<Environment>(custom_environment);
    } else {
        // Create default environment optimized for reflections
        reflection_env.instantiate();
        reflection_env->set_background(Environment::BG_CLEAR_COLOR);        // Clear background
        reflection_env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);  // Use ambient color
        reflection_env->set_ambient_light_color(Color(0.8, 0.8, 0.8));     // Neutral gray
        reflection_env->set_ambient_light_energy(1.0);                     // Standard intensity
    }
    
    // Apply environment to reflection camera
    reflect_camera->set_environment(reflection_env);
}

/**
 * @brief Locates the editor helper singleton for viewport size detection (required for Editor Cam and ViewPortSync)
 * 
 * In editor mode, we need to detect the size of the 3D viewport
 * window to match reflection resolution to the editor view.
 * This function finds the PlanarReflectorEditorHelper singleton
 * that provides this functionality.
 */
void PlanarReflectorCPP::find_editor_helper()
{
    if (Engine::get_singleton()->is_editor_hint()) {
        // Check if the editor helper singleton is registered
        if (Engine::get_singleton()->has_singleton("PlanarReflectorEditorHelper")) {
            editor_helper = Engine::get_singleton()->get_singleton("PlanarReflectorEditorHelper");
        }
    }
}

/**
 * @brief Sets up compositor effects for advanced reflection features
 * 
 * The Compositor system in Godot 4.4+ allows post-processing effects
 * to be applied to camera rendering. For reflections, we use it to:
 * - Hide geometry that intersects the reflection plane
 * - Fill gaps in reflections (experimental feature)
 * - Adjust reflection appearance at the pixel level
 * @param reflect_cam The reflection camera to apply effects to
 */
void PlanarReflectorCPP::setup_compositor_reflection_effect(Camera3D *reflect_cam) 
{
    if (!reflect_cam) {
        return;
    }
    
    // Priority 1: Use explicitly set compositor
    if(active_compositor.is_valid() && (reflect_cam->get_compositor() != active_compositor))
    {
        reflect_cam->set_compositor(active_compositor);
        update_compositor_parameters();  // Configure effect parameters
        return;
    }

    // Priority 2: Create new compositor if camera doesn't have one
    Ref<Compositor> current_comp = reflect_cam->get_compositor();
    if (!current_comp.is_valid() || current_comp->get_compositor_effects().size() == 0) {
        // Load compositor from resource file and assign to camera
        active_compositor = create_new_compositor();
        reflect_cam->set_compositor(active_compositor);
        update_compositor_parameters();
        return;
    }
}

/**
 * @brief Creates a new compositor by loading from resource file
 * Loads the reflection compositor effect from a .tres resource file
 * that contains pre-configured CompositorEffect settings. The resource
 * is duplicated to ensure each reflection camera gets its own instance.
 * 
 * @return Ref<Compositor> New compositor instance, or empty ref if loading fails
 */
Ref<Compositor> PlanarReflectorCPP::create_new_compositor() 
{
    // Load the pre-configured reflection compositor resource
    Variant loaded_resource = ResourceLoader::get_singleton()->load("res://addons/PlanarReflectorCpp/SupportFiles/reflection_compositor.tres");
    
    if (loaded_resource.get_type() == Variant::OBJECT) 
    {
        // CRITICAL: Create a unique copy using duplicate(true) for deep copy
        // This ensures each camera gets its own effect instance
        Ref<Resource> resource_ref = loaded_resource;
        if (resource_ref.is_valid()) 
        {
            Ref<Resource> unique_copy = resource_ref->duplicate(true); // Deep copy
            Ref<Compositor> compositor = Object::cast_to<Compositor>(unique_copy.ptr());
            return compositor;
        }
    } 

    // Return empty compositor if loading fails
    return Ref<Compositor>();
}

/**
 * @brief Updates compositor effect parameters with current reflection settings
 * 
 * Configures the compositor effect with reflection-specific parameters:
 * - effect_enabled: Whether the effect should be active
 * - fill_enabled: Whether to use experimental gap filling
 * - intersect_height: Y-coordinate of the reflection plane
 */
void PlanarReflectorCPP::update_compositor_parameters()
{
    if (!active_compositor.is_valid()) return;
    
    // Get the first compositor effect (our reflection effect)
    TypedArray<CompositorEffect> effects = active_compositor->get_compositor_effects();
    if (effects.size() > 0) {
        CompositorEffect* effect = Object::cast_to<CompositorEffect>(effects[0]);
        if (effect) {
            // Calculate intersection height - use override or reflector position
            double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
            
            // Update effect parameters
            effect->set("effect_enabled", hide_intersect_reflections);
            effect->set("fill_enabled", fill_reflection_experimental);
            effect->set("intersect_height", height);
        }
    }
}

/**
 * @brief Calculates the reflection plane based on reflector transform
 * 
 * The reflection plane defines the mathematical surface across which
 * the camera view is mirrored. 
 * 
 * @return Plane The reflection plane in world space
 */
Plane PlanarReflectorCPP::calculate_reflection_plane()
{
    if (!is_inside_tree()) {
        return Plane();  // Return invalid plane if not in scene
    }
        
    Transform3D current_transform = get_global_transform();
    
    // Create reflection plane by rotating reflector transform 90 degrees around X
    // This makes the "up" direction (Y) become the plane normal
    Transform3D reflection_transform = current_transform * Transform3D().rotated(Vector3(1, 0, 0), Math_PI / 2.0);
    
    // Extract plane properties from the transformed basis
    Vector3 plane_origin = reflection_transform.get_origin();           // Point on plane
    Vector3 plane_normal = reflection_transform.get_basis().get_column(2).normalized();  // Plane normal
    
    // Create plane using normal and distance from origin
    // Distance = normal • origin (dot product)
    cached_reflection_plane = Plane(plane_normal, plane_origin.dot(plane_normal));
    last_global_transform = current_transform;
    
    return cached_reflection_plane;
}

/**
 * @brief Updates reflection viewport size based on screen/editor size and LOD
 * 
 * Manages the resolution of the reflection viewport and sets the LOD system
 */
void PlanarReflectorCPP::update_reflect_viewport_size()
{
    if (!reflect_viewport) {
        UtilityFunctions::print("[PlanarReflectorCPP] ERROR: update_reflect_viewport_size - reflect_viewport is null");
        return;
    }
    
    // Performance optimization: only check size periodically
    if (frame_counter - last_viewport_check_frame < viewport_check_frequency) {
        return;
    }
    last_viewport_check_frame = frame_counter;
    
    // Get base target size from screen/editor
    Vector2i target_size = get_target_viewport_size();
    Camera3D *active_cam = get_active_camera();
    
    // Apply LOD scaling if enabled and we have an active camera
    if (use_lod && active_cam) {
        target_size = apply_lod_to_size(target_size, active_cam);
    }

    // Apply the calculated size to the viewport
    reflect_viewport->set_size(target_size);
}

/**
 * @brief Main reflection calculation - Updates camera transform and shader parameters
 * 
 * This is the core function that creates the reflection effect by:
 * 1. Calculating the reflection plane from the reflector's position
 * 2. Computing the mirrored camera position across the plane
 * 3. Creating a mirrored camera orientation (basis)
 * 4. Applying any configured offset adjustments
 * 5. Updating shader parameters with the new reflection data
 */
void PlanarReflectorCPP::set_reflection_camera_transform()
{
    // Validate scene tree state
    if (!is_inside_tree()) {
        // UtilityFunctions::print("[PlanarReflectorCPP] ERROR: set_reflection_camera_transform stopped - Not Inside Tree");
        return;
    }
        
    // Validate required cameras exist
    Camera3D *active_camera = get_active_camera();
    if (!active_camera || !reflect_camera) {
        UtilityFunctions::print("[PlanarReflectorCPP] Info: MIssing Camera or Reflect Camera not loaded. Reflections will not show.");
        return;
    }
        
    // Update camera projection settings to match main camera
    update_camera_projection();
    
    // Calculate the mathematical reflection plane
    Plane reflection_plane = calculate_reflection_plane();
    
    // STEP 1: Calculate mirrored camera position
    Vector3 cam_pos = active_camera->get_global_transform().get_origin();
    Vector3 proj_pos = reflection_plane.project(cam_pos);           // Project onto plane
    Vector3 mirrored_pos = cam_pos + (proj_pos - cam_pos) * 2.0;    // Mirror across plane
    
    // STEP 2: Create base transform with mirrored position
    Transform3D base_reflection_transform;
    base_reflection_transform.set_origin(mirrored_pos);
    
    // STEP 3: Calculate mirrored camera orientation (basis)
    Basis main_basis = active_camera->get_global_transform().get_basis();
    Vector3 n = reflection_plane.get_normal();
    
    // Mirror each basis vector by bouncing it off the plane normal
    // bounce() function: vector - 2 * (vector • normal) * normal
    Basis reflection_basis;
    reflection_basis.set_column(0, main_basis.get_column(0).normalized().bounce(n).normalized());  // Right vector
    reflection_basis.set_column(1, main_basis.get_column(1).normalized().bounce(n).normalized());  // Up vector
    reflection_basis.set_column(2, main_basis.get_column(2).normalized().bounce(n).normalized());  // Forward vector
    
    // STEP 4: Combine position and orientation
    base_reflection_transform.set_basis(reflection_basis);
    
    // STEP 5: Apply any configured offset adjustments
    Transform3D final_reflection_transform = apply_reflection_offset(base_reflection_transform);
    
    // STEP 6: Set the calculated transform on the reflection camera
    reflect_camera->set_global_transform(final_reflection_transform);
    
    // STEP 7: Update shader material with new reflection data
    update_shader_parameters();
}

/**
 * @brief Updates shader material parameters with current reflection data
 * 
 * Transfers reflection rendering data to the shader material that
 * displays the reflection effect.
 */
void PlanarReflectorCPP::update_shader_parameters()
{
    // Validate that we have surface materials to work with
    if (get_surface_override_material_count() == 0) {
        // UtilityFunctions::print("[PlanarReflectorCPP] ERROR: update_shader_parameters - No surface material");
        return;
    }
    
    // Get the first material and cast to ShaderMaterial
    ShaderMaterial *material = Object::cast_to<ShaderMaterial>(get_active_material(0).ptr()); 
    if (!material || !reflect_viewport) {
        // UtilityFunctions::print("[PlanarReflectorCPP] Info: Please add a material and reload the scene to enable reflection.");
        return;
    }

    // Get the rendered reflection texture from viewport
    Ref<Texture2D> reflection_texture = reflect_viewport->get_texture();
    bool is_orthogonal = false;
    
    // Determine camera projection type for shader math
    Camera3D *active_cam = get_active_camera();
    if (active_cam) {
        is_orthogonal = (active_cam->get_projection() == Camera3D::PROJECTION_ORTHOGONAL);
    }
    
    // Validate reflection texture quality
    if(reflection_texture.is_null() || reflection_texture.is_valid() == false || 
       reflection_texture->get_size() != reflect_viewport->get_size())
    {
        UtilityFunctions::print("[PlanarReflectorCPP] ERROR: update_shader_parameters - No valid texture found");
    }
    
    // Update all shader parameters for reflection rendering
    material->set_shader_parameter("reflection_screen_texture", reflection_texture);      // Main reflection image
    material->set_shader_parameter("is_orthogonal_camera", is_orthogonal);              // Projection type flag
    material->set_shader_parameter("ortho_uv_scale", ortho_uv_scale);                   // UV scaling for ortho
    material->set_shader_parameter("reflection_offset_enabled", enable_reflection_offset); // Offset system flag
    material->set_shader_parameter("reflection_offset_position", reflection_offset_position); // Position offset
    material->set_shader_parameter("reflection_offset_scale", reflection_offset_scale);  // Scale offset
    material->set_shader_parameter("reflection_plane_normal", cached_reflection_plane.get_normal()); // Plane normal vector
    material->set_shader_parameter("reflection_plane_distance", cached_reflection_plane.d);      // Plane distance
    material->set_shader_parameter("planar_surface_y", get_global_transform().get_origin().y);  // Surface height
}

/**
 * @brief Updates reflection camera projection to match main camera
 */
void PlanarReflectorCPP::update_camera_projection()
{
    Camera3D *active_cam = get_active_camera();
    if (!active_cam || !reflect_camera) {
        return;
    }
    
    // Auto-detect and match main camera projection type
    if (auto_detect_camera_mode) {
        reflect_camera->set_projection(active_cam->get_projection());
    }
    
    // Configure projection-specific parameters
    if (reflect_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL) {
        // Orthogonal: copy and scale the view size
        reflect_camera->set_size(active_cam->get_size() * ortho_scale_multiplier);
    } else {
        // Perspective: copy the field of view
        reflect_camera->set_fov(active_cam->get_fov());
    }
}

/**
 * @brief Clears shader texture references to prevent memory leaks
 * 
 * Called before destroying viewports to ensure shader materials
 * don't hold references to freed textures. 
 */
void PlanarReflectorCPP::clear_shader_texture_references()
{
    if (get_surface_override_material_count() == 0) {
        return;
    }

    Ref<Material> material = get_active_material(0);
    if (material.is_valid() && Object::cast_to<ShaderMaterial>(material.ptr())) 
    {
        ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(material.ptr());
        shader_material->set_shader_parameter("reflection_screen_texture", Variant());
    }
}

/**
 * @brief Applies artistic offset transformations to reflection camera
 * 
 * @param base_transform The calculated reflection transform
 * @return Transform3D The adjusted transform with offsets applied
 */
Transform3D PlanarReflectorCPP::apply_reflection_offset(const Transform3D &base_transform)
{
    // Early exit if offsets are disabled
    if (!enable_reflection_offset) {
        return base_transform;
    }
    
    Transform3D result_transform = base_transform;
    
    switch (offset_blend_mode) {
        case 0: // Add mode - simple addition of offset
            result_transform.set_origin(result_transform.get_origin() + cached_offset_transform.get_origin());
            if (reflection_offset_rotation != Vector3()) {
                result_transform.set_basis(result_transform.get_basis() * cached_offset_transform.get_basis());
            }
            break;
        
        case 1: // Multiply mode - relative to current transform
            result_transform = result_transform * cached_offset_transform;
            break;
        
        case 2: // Screen space shift mode - offset relative to camera view
            if (main_camera) {
                Vector3 view_offset = main_camera->get_global_transform().get_basis().xform(cached_offset_transform.get_origin());
                result_transform.set_origin(result_transform.get_origin() + view_offset);
                result_transform.set_basis(result_transform.get_basis() * cached_offset_transform.get_basis());
            }
            break;
    }
    
    return result_transform;
}

/**
 * @brief Determines appropriate viewport size based on context
 */
Vector2i PlanarReflectorCPP::get_target_viewport_size()
{
    Vector2i target_size = Vector2i(1920, 1080); // High-quality fallback default
    
    if (Engine::get_singleton()->is_editor_hint()) {
        // Editor mode: try to get 3D viewport size from helper
        if (editor_helper && editor_helper->has_method("get_editor_viewport_size")) {
            Variant result = editor_helper->call("get_editor_viewport_size");
            if (result.get_type() == Variant::VECTOR2I) {
                target_size = result;
            } else if (get_viewport()) {
                target_size = get_viewport()->get_visible_rect().size;
            }
        } else if (get_viewport()) {
            target_size = get_viewport()->get_visible_rect().size;
        }
    } else {
        // Game mode: use main viewport size
        if (get_viewport()) {
            target_size = get_viewport()->get_visible_rect().size;
        }
    }
    
    return target_size;
}

/**
 * @brief Applies Level-of-Detail scaling based on camera distance
 * 
 * Implements a distance-based quality reduction system to maintain
 * performance when reflections are far from the camera.
 * 
 * @param target_size Base target size before LOD scaling
 * @param active_cam Camera to measure distance from
 * @return Vector2i Size after LOD scaling is applied
 */
Vector2i PlanarReflectorCPP::apply_lod_to_size(Vector2i target_size, Camera3D *active_cam)
{
    if (!is_inside_tree()) {
        return target_size;
    }
        
    double distance = get_global_transform().get_origin().distance_to(active_cam->get_global_transform().get_origin());
    
    // Cache LOD calculations when distance hasn't changed much
    // Reduces CPU overhead by avoiding repeated calculations
    if (Math::abs(distance - last_distance_check) > 1.0) {
        double lod_factor = 1.0;  // Start with full quality
        
        if (distance > lod_distance_near) {
            // Calculate interpolation factor between near and far distances
            double lerp_factor = Math::clamp((distance - lod_distance_near) / (lod_distance_far - lod_distance_near), 0.0, 1.0);
            // Interpolate between full quality (1.0) and reduced quality
            lod_factor = Math::lerp(1.0, lod_resolution_multiplier, lerp_factor);
        }
        
        cached_lod_factor = lod_factor;
        last_distance_check = distance;
    }
    
    // Apply cached LOD factor to target size
    Vector2i result_size = Vector2i((double)target_size.x * cached_lod_factor, (double)target_size.y * cached_lod_factor);
    
    // Enforce minimum resolution to prevent degenerate cases
    result_size.x = Math::max(result_size.x, 128);
    result_size.y = Math::max(result_size.y, 128);
    
    return result_size;
}

/**
 * @brief Sets the editor camera reference for editor mode operation
 * 
 * Called by the editor plugin to provide the current 3D viewport camera.
 * This allows reflections to work properly in the editor by tracking
 * the editor camera instead of a scene camera. used by editor plugin helper
 * 
 * @param viewport_camera The current editor 3D viewport camera
 */
void PlanarReflectorCPP::set_editor_camera(Camera3D *viewport_camera)
{
    // UtilityFunctions::print("[PlanarReflectorCPP2] set_editor_camera called");

    editor_camera = viewport_camera;
    
    // Immediately update reflection system with new camera
    update_reflect_viewport_size();      // Match viewport to editor size
    set_reflection_camera_transform();   // Recalculate reflection
    update_compositor_parameters();      // Update effect parameters
}

/**
 * @brief Gets the currently active camera based on context. Also used by editor plugin helper
 * 
 */
Camera3D* PlanarReflectorCPP::get_active_camera()
{
    if (Engine::get_singleton()->is_editor_hint()) {
        // Editor mode: prefer editor camera, fallback to main camera
        return editor_camera ? editor_camera : main_camera;
    }
    
    // Game mode: validate main camera exists
    if (!main_camera) {
        return nullptr;
    }

    return main_camera;
}

/**
 * @brief Returns whether this reflector is currently active. used by editor plugin helper
 * 
 */
bool PlanarReflectorCPP::is_planar_reflector_active()
{
    return is_active;
}

/**
 * @brief Cleanup function - Called when node exits scene tree
 * 
 * Performs critical cleanup to prevent crashes and memory leaks
 */
void PlanarReflectorCPP::_exit_tree()
{
    // CRITICAL: Clear shader references FIRST to prevent crashes
    // This must happen before Godot frees the viewport and camera nodes
    clear_shader_texture_references();
}

/**
 * @brief Binds methods and properties to Godot's class system
 * 
 * This function exposes the C++ class to GDScript and the editor
 * by registering all methods and properties with Godot's ClassDB.
 */
void PlanarReflectorCPP::_bind_methods() 
{
    // === CORE PROPERTIES ===
    
    // Main activity toggle - Controls whether reflections are calculated and rendered
    ClassDB::bind_method(D_METHOD("set_is_active", "p_active"), &PlanarReflectorCPP::set_is_active);
    ClassDB::bind_method(D_METHOD("get_is_active"), &PlanarReflectorCPP::get_is_active);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_active", PROPERTY_HINT_NONE, "Enable/disable reflection calculations and rendering"), "set_is_active", "get_is_active");

    // Main camera reference - The camera that reflections will mirror
    ClassDB::bind_method(D_METHOD("set_main_camera", "p_camera"), &PlanarReflectorCPP::set_main_camera);
    ClassDB::bind_method(D_METHOD("get_main_camera"), &PlanarReflectorCPP::get_main_camera);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "main_camera", PROPERTY_HINT_NODE_TYPE, "Camera3D", PROPERTY_USAGE_DEFAULT, "The camera to create reflections for (used in game mode)"), "set_main_camera", "get_main_camera");
    
    // Reflection resolution - Higher values = better quality but lower performance
    ClassDB::bind_method(D_METHOD("set_reflection_camera_resolution", "p_resolution"), &PlanarReflectorCPP::set_reflection_camera_resolution);
    ClassDB::bind_method(D_METHOD("get_reflection_camera_resolution"), &PlanarReflectorCPP::get_reflection_camera_resolution);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "reflection_camera_resolution", PROPERTY_HINT_NONE, "Resolution of the reflection viewport. Higher values improve quality but reduce performance"), "set_reflection_camera_resolution", "get_reflection_camera_resolution");

    // === CAMERA CONTROLS GROUP ===
    ADD_GROUP("Camera Controls", "");
    
    // Orthogonal projection scaling - Adjusts the size of orthogonal camera views
    ClassDB::bind_method(D_METHOD("set_ortho_scale_multiplier", "p_multiplier"), &PlanarReflectorCPP::set_ortho_scale_multiplier);
    ClassDB::bind_method(D_METHOD("get_ortho_scale_multiplier"), &PlanarReflectorCPP::get_ortho_scale_multiplier);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ortho_scale_multiplier", PROPERTY_HINT_RANGE, "0.1,10.0,0.1", PROPERTY_USAGE_DEFAULT, "Multiplier for orthogonal camera size. Values > 1.0 zoom out, < 1.0 zoom in"), "set_ortho_scale_multiplier", "get_ortho_scale_multiplier");

    // UV coordinate scaling for orthogonal projections
    ClassDB::bind_method(D_METHOD("set_ortho_uv_scale", "p_scale"), &PlanarReflectorCPP::set_ortho_uv_scale);
    ClassDB::bind_method(D_METHOD("get_ortho_uv_scale"), &PlanarReflectorCPP::get_ortho_uv_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ortho_uv_scale", PROPERTY_HINT_RANGE, "0.1,10.0,0.1", PROPERTY_USAGE_DEFAULT, "UV coordinate scaling for orthogonal projections. Affects reflection texture mapping"), "set_ortho_uv_scale", "get_ortho_uv_scale");
    
    // Automatic camera mode detection - Matches main camera's projection type
    ClassDB::bind_method(D_METHOD("set_auto_detect_camera_mode", "p_auto_detect"), &PlanarReflectorCPP::set_auto_detect_camera_mode);
    ClassDB::bind_method(D_METHOD("get_auto_detect_camera_mode"), &PlanarReflectorCPP::get_auto_detect_camera_mode);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_detect_camera_mode", PROPERTY_HINT_NONE, "Automatically match the main camera's projection mode (perspective/orthogonal)"), "set_auto_detect_camera_mode", "get_auto_detect_camera_mode");

    // === REFLECTION LAYERS AND ENVIRONMENT GROUP ===
    ADD_GROUP("Reflection Layers and Environment", "");
    
    // 3D render layers - Controls which objects appear in reflections
    ClassDB::bind_method(D_METHOD("set_reflection_layers", "p_layers"), &PlanarReflectorCPP::set_reflection_layers);
    ClassDB::bind_method(D_METHOD("get_reflection_layers"), &PlanarReflectorCPP::get_reflection_layers);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_layers", PROPERTY_HINT_LAYERS_3D_RENDER, "", PROPERTY_USAGE_DEFAULT, "3D render layers visible in reflections. Use to exclude specific objects from reflections"), "set_reflection_layers", "get_reflection_layers");

    // Custom environment toggle - Whether to use a custom Environment resource
    ClassDB::bind_method(D_METHOD("set_use_custom_environment", "p_use_custom"), &PlanarReflectorCPP::set_use_custom_environment);
    ClassDB::bind_method(D_METHOD("get_use_custom_environment"), &PlanarReflectorCPP::get_use_custom_environment);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_custom_environment", PROPERTY_HINT_NONE, "Use custom Environment resource instead of auto-generated one"), "set_use_custom_environment", "get_use_custom_environment");

    // Custom environment resource - Provides full control over reflection rendering environment
    ClassDB::bind_method(D_METHOD("set_custom_environment", "p_environment"), &PlanarReflectorCPP::set_custom_environment);
    ClassDB::bind_method(D_METHOD("get_custom_environment"), &PlanarReflectorCPP::get_custom_environment);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "custom_environment", PROPERTY_HINT_RESOURCE_TYPE, "Environment", PROPERTY_USAGE_DEFAULT, "Custom Environment for reflection rendering (skybox, fog, lighting)"), "set_custom_environment", "get_custom_environment");

    // === REFLECTION COMPOSITOR EFFECTS GROUP ===
    ADD_GROUP("Reflection Compositor Effects", "");
    
    // Active compositor - The Compositor resource containing reflection effects
    ClassDB::bind_method(D_METHOD("set_active_compositor", "p_compositor"), &PlanarReflectorCPP::set_active_compositor);
    ClassDB::bind_method(D_METHOD("get_active_compositor"), &PlanarReflectorCPP::get_active_compositor);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "active_compositor", PROPERTY_HINT_RESOURCE_TYPE, "Compositor", PROPERTY_USAGE_DEFAULT, "Compositor containing reflection post-processing effects"), "set_active_compositor", "get_active_compositor");

    // Hide intersecting reflections - Removes geometry that clips through the reflection surface
    ClassDB::bind_method(D_METHOD("set_hide_intersect_reflections", "p_hide"), &PlanarReflectorCPP::set_hide_intersect_reflections);
    ClassDB::bind_method(D_METHOD("get_hide_intersect_reflections"), &PlanarReflectorCPP::get_hide_intersect_reflections);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hide_intersect_reflections", PROPERTY_HINT_NONE, "Hide geometry that intersects the reflection plane for more realistic reflections"), "set_hide_intersect_reflections", "get_hide_intersect_reflections");

    // Y-axis height override - Manually set the reflection plane height
    ClassDB::bind_method(D_METHOD("set_override_YAxis_height", "p_override"), &PlanarReflectorCPP::set_override_YAxis_height);
    ClassDB::bind_method(D_METHOD("get_override_YAxis_height"), &PlanarReflectorCPP::get_override_YAxis_height);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "override_YAxis_height", PROPERTY_HINT_NONE, "Override automatic reflection plane height with custom value"), "set_override_YAxis_height", "get_override_YAxis_height");

    // Custom Y-axis height value
    ClassDB::bind_method(D_METHOD("set_new_YAxis_height", "p_height"), &PlanarReflectorCPP::set_new_YAxis_height);
    ClassDB::bind_method(D_METHOD("get_new_YAxis_height"), &PlanarReflectorCPP::get_new_YAxis_height);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "new_YAxis_height", PROPERTY_HINT_NONE, "Custom Y coordinate for the reflection plane (when override is enabled)"), "set_new_YAxis_height", "get_new_YAxis_height");

    // Experimental gap filling - Attempts to fill holes in reflections
    ClassDB::bind_method(D_METHOD("set_fill_reflection_experimental", "p_fill"), &PlanarReflectorCPP::set_fill_reflection_experimental);
    ClassDB::bind_method(D_METHOD("get_fill_reflection_experimental"), &PlanarReflectorCPP::get_fill_reflection_experimental);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fill_reflection_experimental", PROPERTY_HINT_NONE, "Experimental feature to fill gaps in reflections. May affect performance"), "set_fill_reflection_experimental", "get_fill_reflection_experimental");

    // === REFLECTION OFFSET CONTROL GROUP ===
    ADD_GROUP("Reflection Offset Control", "");
    
    // Enable artistic offset adjustments
    ClassDB::bind_method(D_METHOD("set_enable_reflection_offset", "p_enable"), &PlanarReflectorCPP::set_enable_reflection_offset);
    ClassDB::bind_method(D_METHOD("get_enable_reflection_offset"), &PlanarReflectorCPP::get_enable_reflection_offset);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_reflection_offset", PROPERTY_HINT_NONE, "Enable artistic offset adjustments to reflection camera position and rotation"), "set_enable_reflection_offset", "get_enable_reflection_offset");

    // Position offset for reflection camera
    ClassDB::bind_method(D_METHOD("set_reflection_offset_position", "p_position"), &PlanarReflectorCPP::set_reflection_offset_position);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_position"), &PlanarReflectorCPP::get_reflection_offset_position);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "reflection_offset_position", PROPERTY_HINT_NONE, "Position offset applied to reflection camera for artistic adjustments"), "set_reflection_offset_position", "get_reflection_offset_position");

    // Rotation offset for reflection camera
    ClassDB::bind_method(D_METHOD("set_reflection_offset_rotation", "p_rotation"), &PlanarReflectorCPP::set_reflection_offset_rotation);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_rotation"), &PlanarReflectorCPP::get_reflection_offset_rotation);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "reflection_offset_rotation", PROPERTY_HINT_NONE, "Rotation offset in degrees applied to reflection camera"), "set_reflection_offset_rotation", "get_reflection_offset_rotation");

    // Scale factor for offset calculations
    ClassDB::bind_method(D_METHOD("set_reflection_offset_scale", "p_scale"), &PlanarReflectorCPP::set_reflection_offset_scale);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_scale"), &PlanarReflectorCPP::get_reflection_offset_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_offset_scale", PROPERTY_HINT_RANGE, "0.1,10.0,0.1", PROPERTY_USAGE_DEFAULT, "Scale multiplier for offset position values"), "set_reflection_offset_scale", "get_reflection_offset_scale");

    // Offset blend mode - How offsets are applied to the base reflection transform
    ClassDB::bind_method(D_METHOD("set_offset_blend_mode", "p_mode"), &PlanarReflectorCPP::set_offset_blend_mode);
    ClassDB::bind_method(D_METHOD("get_offset_blend_mode"), &PlanarReflectorCPP::get_offset_blend_mode);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "offset_blend_mode", PROPERTY_HINT_ENUM, "Add,Multiply,Screen Space Shift", PROPERTY_USAGE_DEFAULT, "How offset transforms are combined with reflection transform"), "set_offset_blend_mode", "get_offset_blend_mode");

    // === PERFORMANCE CONTROLS GROUP ===
    ADD_GROUP("Performance Controls", "");
    
    // Update frequency - Balance between quality and performance
    ClassDB::bind_method(D_METHOD("set_update_frequency", "p_frequency"), &PlanarReflectorCPP::set_update_frequency);
    ClassDB::bind_method(D_METHOD("get_update_frequency"), &PlanarReflectorCPP::get_update_frequency);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "update_frequency", PROPERTY_HINT_RANGE, "1,10,1", PROPERTY_USAGE_DEFAULT, "Update reflections every N frames. Higher = better performance but choppier reflections"), "set_update_frequency", "get_update_frequency");

    // Level-of-detail system toggle
    ClassDB::bind_method(D_METHOD("set_use_lod", "p_use_lod"), &PlanarReflectorCPP::set_use_lod);
    ClassDB::bind_method(D_METHOD("get_use_lod"), &PlanarReflectorCPP::get_use_lod);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_lod", PROPERTY_HINT_NONE, "Enable distance-based level of detail (LOD) to reduce resolution when far from camera"), "set_use_lod", "get_use_lod");

    // LOD near distance - Full quality threshold
    ClassDB::bind_method(D_METHOD("set_lod_distance_near", "p_distance"), &PlanarReflectorCPP::set_lod_distance_near);
    ClassDB::bind_method(D_METHOD("get_lod_distance_near"), &PlanarReflectorCPP::get_lod_distance_near);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance_near", PROPERTY_HINT_RANGE, "1.0,100.0,0.1", PROPERTY_USAGE_DEFAULT, "Distance where LOD scaling begins. Reflections at this distance or closer use full resolution"), "set_lod_distance_near", "get_lod_distance_near");

    // LOD far distance - Minimum quality threshold
    ClassDB::bind_method(D_METHOD("set_lod_distance_far", "p_distance"), &PlanarReflectorCPP::set_lod_distance_far);
    ClassDB::bind_method(D_METHOD("get_lod_distance_far"), &PlanarReflectorCPP::get_lod_distance_far);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance_far", PROPERTY_HINT_RANGE, "10.0,200.0,1.0", PROPERTY_USAGE_DEFAULT, "Distance where minimum LOD quality is reached. Reflections beyond this distance use lowest resolution"), "set_lod_distance_far", "get_lod_distance_far");

    // LOD resolution multiplier - Quality reduction factor
    ClassDB::bind_method(D_METHOD("set_lod_resolution_multiplier", "p_multiplier"), &PlanarReflectorCPP::set_lod_resolution_multiplier);
    ClassDB::bind_method(D_METHOD("get_lod_resolution_multiplier"), &PlanarReflectorCPP::get_lod_resolution_multiplier);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_resolution_multiplier", PROPERTY_HINT_RANGE, "0.1,1.0,0.01", PROPERTY_USAGE_DEFAULT, "Resolution multiplier for distant reflections. 0.5 = half resolution, 0.25 = quarter resolution"), "set_lod_resolution_multiplier", "get_lod_resolution_multiplier");

    // === UTILITY METHODS FOR EDITOR INTEGRATION ===
    // These methods are critical for the editor plugin to function properly
    
    // Editor camera integration - Called by editor plugin
    ClassDB::bind_method(D_METHOD("set_editor_camera", "viewport_camera"), &PlanarReflectorCPP::set_editor_camera);
    ClassDB::bind_method(D_METHOD("get_active_camera"), &PlanarReflectorCPP::get_active_camera);
    ClassDB::bind_method(D_METHOD("is_planar_reflector_active"), &PlanarReflectorCPP::is_planar_reflector_active);
    
    // Manual update methods - For debugging and plugin integration
    ClassDB::bind_method(D_METHOD("update_reflect_viewport_size"), &PlanarReflectorCPP::update_reflect_viewport_size);
    ClassDB::bind_method(D_METHOD("set_reflection_camera_transform"), &PlanarReflectorCPP::set_reflection_camera_transform);
    
    // === DEBUG AND MAINTENANCE METHODS ===
    // These methods allow manual control over the reflection system
    
    // Core setup methods
    ClassDB::bind_method(D_METHOD("setup_reflection_camera_and_viewport"), &PlanarReflectorCPP::setup_reflection_camera_and_viewport);
    ClassDB::bind_method(D_METHOD("initial_setup"), &PlanarReflectorCPP::initial_setup);
    ClassDB::bind_method(D_METHOD("setup_compositor_reflection_effect", "reflect_cam"), &PlanarReflectorCPP::setup_compositor_reflection_effect);

    // Deferred setup methods
    ClassDB::bind_method(D_METHOD("create_viewport_deferred"), &PlanarReflectorCPP::create_viewport_deferred);
    ClassDB::bind_method(D_METHOD("finalize_setup"), &PlanarReflectorCPP::finalize_setup);
    
    // Maintenance methods
    ClassDB::bind_method(D_METHOD("clear_shader_texture_references"), &PlanarReflectorCPP::clear_shader_texture_references);
}

// ========================================
// PROPERTY SETTERS AND GETTERS IMPLEMENTATION
// ========================================
/**
 @brief Sets the active state of the reflection system
 */
void PlanarReflectorCPP::set_is_active(const bool p_active) { is_active = p_active; }

/**
 * @brief Gets the current active state
 */
bool PlanarReflectorCPP::get_is_active() const { return is_active; }

/**
 * @brief Sets the main camera for reflection calculations
 */
void PlanarReflectorCPP::set_main_camera(Camera3D *p_camera) 
{
    main_camera = Object::cast_to<Camera3D>(p_camera);
    
    // Update reflection camera if both cameras exist
    if (reflect_camera && main_camera) {
        // Copy camera attributes for consistent rendering
        reflect_camera->set_attributes(main_camera->get_attributes());
        reflect_camera->set_doppler_tracking(main_camera->get_doppler_tracking());
        
        // Refresh environment to ensure consistency
        setup_reflection_environment();
    }
}

Camera3D* PlanarReflectorCPP::get_main_camera() const { return main_camera; }

/**
 * @brief Sets the reflection viewport resolution
 * 
 * Higher resolutions provide better quality but reduce performance.
 * The viewport is immediately resized if it exists.
 * 
 * @param p_resolution New resolution in pixels (width, height)
 */
void PlanarReflectorCPP::set_reflection_camera_resolution(const Vector2i p_resolution) 
{ 
    reflection_camera_resolution = p_resolution;
    
    // Apply new resolution immediately if viewport exists
    if (reflect_viewport) {
        reflect_viewport->set_size(reflection_camera_resolution);
    }
}

Vector2i PlanarReflectorCPP::get_reflection_camera_resolution() const { return reflection_camera_resolution; }

// === CAMERA CONTROLS GETTERS/SETTERS ===

void PlanarReflectorCPP::set_ortho_scale_multiplier(double p_multiplier) { ortho_scale_multiplier = p_multiplier; }
double PlanarReflectorCPP::get_ortho_scale_multiplier() const { return ortho_scale_multiplier; }

void PlanarReflectorCPP::set_ortho_uv_scale(double p_scale) { ortho_uv_scale = p_scale; }
double PlanarReflectorCPP::get_ortho_uv_scale() const { return ortho_uv_scale; }

void PlanarReflectorCPP::set_auto_detect_camera_mode(bool p_auto_detect) { auto_detect_camera_mode = p_auto_detect; }
bool PlanarReflectorCPP::get_auto_detect_camera_mode() const { return auto_detect_camera_mode; }

void PlanarReflectorCPP::set_reflection_layers(int p_layers)
{
    reflection_layers = p_layers;    
    
    // Apply layer mask to reflection camera immediately
    if (reflect_camera) {
        int cull_mask = reflection_layers;
        reflect_camera->set_cull_mask(cull_mask);
        
        // Check if layer 1 is active (bit 0)
        is_layer_one_active = bool(cull_mask & (1 << 0));
    }
}

int PlanarReflectorCPP::get_reflection_layers() const { return reflection_layers; }

void PlanarReflectorCPP::set_use_custom_environment(bool p_use_custom)
{
    use_custom_environment = p_use_custom;
    
    // Update environment immediately if we're ready
    if (is_inside_tree()) {
        setup_reflection_environment();
    }
}

bool PlanarReflectorCPP::get_use_custom_environment() const { return use_custom_environment; }

void PlanarReflectorCPP::set_custom_environment(Environment *p_environment)
{
    custom_environment = Object::cast_to<Environment>(p_environment);
    
    // Apply new environment if custom environments are enabled
    if (use_custom_environment && is_inside_tree()) {
        setup_reflection_environment();
    }
}

Environment* PlanarReflectorCPP::get_custom_environment() const { return custom_environment; }

void PlanarReflectorCPP::set_active_compositor(Compositor *p_compositor)
{
    if (p_compositor) {
        active_compositor = Ref<Compositor>(p_compositor);
    } else {
        active_compositor.unref();  // Properly clear the Ref
    }
    
    // Apply compositor immediately if reflection system is ready
    if (reflect_camera && is_inside_tree()) {
        setup_compositor_reflection_effect(reflect_camera);
    }
}

Compositor* PlanarReflectorCPP::get_active_compositor() const 
{ 
    return active_compositor.ptr(); 
}

void PlanarReflectorCPP::set_hide_intersect_reflections(bool p_hide)
{
    hide_intersect_reflections = p_hide;
    
    // Update compositor effect parameters immediately
    if (reflect_camera && is_inside_tree()) {
        update_compositor_parameters();
    }
}

bool PlanarReflectorCPP::get_hide_intersect_reflections() const { return hide_intersect_reflections; }

void PlanarReflectorCPP::set_override_YAxis_height(bool p_override)
{
    override_YAxis_height = p_override;
    
    // Update compositor parameters with new height setting
    if (reflect_camera && is_inside_tree()) {
        update_compositor_parameters();
    }
}

bool PlanarReflectorCPP::get_override_YAxis_height() const { return override_YAxis_height; }

void PlanarReflectorCPP::set_new_YAxis_height(double p_height)
{
    new_YAxis_height = p_height;
    
    // Update compositor parameters with new height value
    if (reflect_camera && is_inside_tree()) {
        update_compositor_parameters();
    }
}

double PlanarReflectorCPP::get_new_YAxis_height() const { return new_YAxis_height; }

void PlanarReflectorCPP::set_fill_reflection_experimental(bool p_fill)
{
    fill_reflection_experimental = p_fill;
    
    // Update compositor parameters with new fill setting
    if (reflect_camera && is_inside_tree()) {
        update_compositor_parameters();
    }
}

bool PlanarReflectorCPP::get_fill_reflection_experimental() const { return fill_reflection_experimental; }

void PlanarReflectorCPP::set_enable_reflection_offset(bool p_enable) { enable_reflection_offset = p_enable; }
bool PlanarReflectorCPP::get_enable_reflection_offset() const { return enable_reflection_offset; }

void PlanarReflectorCPP::set_reflection_offset_position(const Vector3 &p_position) { reflection_offset_position = p_position; }
Vector3 PlanarReflectorCPP::get_reflection_offset_position() const { return reflection_offset_position; }

void PlanarReflectorCPP::set_reflection_offset_rotation(const Vector3 &p_rotation) { reflection_offset_rotation = p_rotation; }
Vector3 PlanarReflectorCPP::get_reflection_offset_rotation() const { return reflection_offset_rotation; }

void PlanarReflectorCPP::set_reflection_offset_scale(double p_scale) { reflection_offset_scale = p_scale; }
double PlanarReflectorCPP::get_reflection_offset_scale() const { return reflection_offset_scale; }

void PlanarReflectorCPP::set_offset_blend_mode(int p_mode) { offset_blend_mode = Math::clamp(p_mode, 0, 2); }
int PlanarReflectorCPP::get_offset_blend_mode() const { return offset_blend_mode; }

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