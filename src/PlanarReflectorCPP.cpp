//PlanarReflectorCPP2.cpp FILE - SIMPLIFIED VIEWPORT MANAGEMENT
#include "PlanarReflectorCPP.h"

// Godot includes
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
#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

PlanarReflectorCPP::PlanarReflectorCPP() 
{  
    // UtilityFunctions::print("[PlanarReflectorCPP2] Constructor Started");
    // Initialize with safe defaults
    is_active = true;
    reflection_camera_resolution = Vector2i(1920, 1080);
    ortho_scale_multiplier = 1.0;
    ortho_uv_scale = 1.0;
    auto_detect_camera_mode = true;
    reflection_layers = 1;
    use_custom_environment = false;
    
    // Compositor Effects initialization
    hide_intersect_reflections = true;
    override_YAxis_height = false;
    new_YAxis_height = 0.0;
    fill_reflection_experimental = false;
    
    enable_reflection_offset = false;
    reflection_offset_position = Vector3(0.0, 0.0, 0.0);
    reflection_offset_rotation = Vector3(0.0, 0.0, 0.0);
    reflection_offset_scale = 1.0;
    offset_blend_mode = 0;
    
    update_frequency = 3;
    use_lod = true;
    lod_distance_near = 10.0;
    lod_distance_far = 25.0;
    lod_resolution_multiplier = 0.45;
    
    // Initialize internal variables
    frame_counter = 0;
    position_threshold = 0.01;
    rotation_threshold = 0.001;
    is_layer_one_active = true;
    
    // Initialize performance caches
    last_viewport_check_frame = -1;
    viewport_check_frequency = 5;
    last_distance_check = -1.0;
    cached_lod_factor = 1.0;
}

PlanarReflectorCPP::~PlanarReflectorCPP() 
{
    // Cleanup handled by _exit_tree
    // UtilityFunctions::print("[PlanarReflectorCPP2] DEStructor Started");

}

void PlanarReflectorCPP::_ready() 
{
    // UtilityFunctions::print("[PlanarReflectorCPP2] Ready Started");
    add_to_group("planar_reflectors");
    clear_shader_texture_references();
    call_deferred("initial_setup");
}

void PlanarReflectorCPP::_notification(int what)
{
    if (what == NOTIFICATION_TRANSFORM_CHANGED) {
        if (reflect_camera && reflect_camera->get_compositor().is_valid()) 
        {
            update_reflect_viewport_size();
            set_reflection_camera_transform();
            update_compositor_parameters();
        }
    }
}

void PlanarReflectorCPP::initial_setup()
{
    // UtilityFunctions::print("[PlanarReflectorCPP2] Initial Setup started");

    if (Engine::get_singleton()->is_editor_hint()) {
        find_editor_helper();
    }

    // CRITICAL FIX: Ensure we're in tree before viewport creation
    if (!is_inside_tree()) {
        call_deferred("initial_setup");
        return;
    }

    setup_reflection_camera_and_viewport();
    
    // CRITICAL FIX: Defer these until viewport is ready
    call_deferred("finalize_setup");
}

void PlanarReflectorCPP::finalize_setup()
{
    update_reflect_viewport_size();
    set_reflection_camera_transform();
    // UtilityFunctions::print("[PlanarReflectorCPP2] Initial Setup Completed");
}

void PlanarReflectorCPP::_process(double delta) 
{
    
    if (!is_inside_tree() || !is_active) {
        // UtilityFunctions::print("[PlanarReflectorCPP] ERROR: _process - Not Inside Tree or is not active");
        return;
    }
     
    frame_counter++;
    
    // Less frequent viewport size updates 
    if (viewport_check_frequency > 0 && frame_counter % viewport_check_frequency == 0) {
        update_reflect_viewport_size();
    }
    
    bool should_update = (update_frequency > 0 && frame_counter % update_frequency == 0);

    if (should_update) {
        Camera3D *active_cam = get_active_camera();
        if (active_cam) {
            set_reflection_camera_transform();
        }
    }
}

void PlanarReflectorCPP::setup_reflection_camera_and_viewport()
{
    // UtilityFunctions::print("[PlanarReflectorCPP2] setup_reflection_camera_and_viewport called");

    // CRITICAL FIX: Clear shader references BEFORE destroying viewport
    clear_shader_texture_references();
    
    // Safe cleanup of existing viewport
    if (reflect_viewport) {
        if (reflect_viewport->is_inside_tree()) {
            reflect_viewport->get_parent()->remove_child(reflect_viewport);
        }
        reflect_viewport->queue_free();
        reflect_viewport = nullptr;
    }
    
    if (reflect_camera) {
        reflect_camera = nullptr; // Will be freed with viewport
    }

    // CRITICAL FIX: Wait one frame before creating new viewport
    if (frame_counter > 0) {
        call_deferred("create_viewport_deferred");
        return;
    }
    
    create_viewport_deferred();
}

void PlanarReflectorCPP::create_viewport_deferred()
{
    // Create new viewport with unique name to avoid path conflicts
    reflect_viewport = memnew(SubViewport);
    String unique_name = "ReflectionViewPort";
    reflect_viewport->set_name(unique_name);
    
    add_child(reflect_viewport);
    
    // CRITICAL FIX: Set viewport properties before adding camera
    reflect_viewport->set_size(reflection_camera_resolution);
    reflect_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
    reflect_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);
    reflect_viewport->set_positional_shadow_atlas_size(2048);
    reflect_viewport->set_use_own_world_3d(false);
    reflect_viewport->set_transparent_background(true);
    reflect_viewport->set_handle_input_locally(false);

    // Create camera
    reflect_camera = memnew(Camera3D);
    reflect_camera->set_name("ReflectCamera");
    reflect_viewport->add_child(reflect_camera);
    
    int cull_mask = reflection_layers;
    reflect_camera->set_cull_mask(cull_mask);
    is_layer_one_active = bool(cull_mask & (1 << 0));
    
    if (main_camera) {
        reflect_camera->set_attributes(main_camera->get_attributes());
        reflect_camera->set_doppler_tracking(main_camera->get_doppler_tracking());
    }
    
    reflect_camera->set_current(true);
    setup_reflection_environment();
    
    if (reflect_camera) {
        call_deferred("setup_compositor_reflection_effect", reflect_camera);
    }
}

void PlanarReflectorCPP::setup_reflection_environment()
{
    if (!reflect_camera) {
        return;
    }
    
    Ref<Environment> reflection_env;
    if (use_custom_environment && custom_environment) {
        reflection_env = Ref<Environment>(custom_environment);
    } else {
        reflection_env.instantiate();
        reflection_env->set_background(Environment::BG_CLEAR_COLOR);
        reflection_env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
        reflection_env->set_ambient_light_color(Color(0.8, 0.8, 0.8));
        reflection_env->set_ambient_light_energy(1.0);
    }
    
    reflect_camera->set_environment(reflection_env);
}

void PlanarReflectorCPP::find_editor_helper()
{
    // UtilityFunctions::print("[PlanarReflectorCPP2] find_editor_helper called");

    if (Engine::get_singleton()->is_editor_hint()) {
        if (Engine::get_singleton()->has_singleton("PlanarReflectorEditorHelper")) {
            editor_helper = Engine::get_singleton()->get_singleton("PlanarReflectorEditorHelper");
        }
    }
}

void PlanarReflectorCPP::setup_compositor_reflection_effect(Camera3D *reflect_cam) 
{
    // UtilityFunctions::print("[PlanarReflectorCPP2] setup_compositor_reflection_effect - Called");

    if (!reflect_cam) {
        return;
    }
    
    if(active_compositor.is_valid() && (reflect_cam->get_compositor() != active_compositor))
    {
        // UtilityFunctions::print("[PlanarReflectorCPP2] CAM_COMP: Use Exported Active Compositor to set in Reflect Camera");
        reflect_cam->set_compositor(active_compositor);
        update_compositor_parameters();
        return;
    }

    //create_new_compositor();
    //    // Case 2: Check if camera has an empty/default compositor
    Ref<Compositor> current_comp = reflect_cam->get_compositor();
    if (!current_comp.is_valid() || current_comp->get_compositor_effects().size() == 0) {
        {
            // UtilityFunctions::print("[PlanarReflectorCPP2] CAM_COMP:Creating New Compositor");
            active_compositor = create_new_compositor();

            // active_compositor = compositor.ptr();
            reflect_cam->set_compositor(active_compositor);
            update_compositor_parameters();
            return;
        }
    }
}

Ref<Compositor> PlanarReflectorCPP::create_new_compositor() 
{
        // UtilityFunctions::print("[PlanarReflectorCPP2] CAM_COMP: create_new_compositor CALLED");

    Variant loaded_resource = ResourceLoader::get_singleton()->load("res://addons/PlanarReflectorCpp/SupportFiles/reflection_compositor.tres");
    
    if (loaded_resource.get_type() == Variant::OBJECT) 
    {
        // UtilityFunctions::print("[PlanarReflectorCPP2] CAM_COMP: Loaded Resource is an Object");
        
        // CRITICAL: Create a unique copy using duplicate(true) for deep copy
        Ref<Resource> resource_ref = loaded_resource;
        if (resource_ref.is_valid()) 
        {
            Ref<Resource> unique_copy = resource_ref->duplicate(true); // Deep copy
            Ref<Compositor> compositor = Object::cast_to<Compositor>(unique_copy.ptr());
            
            // UtilityFunctions::print("[PlanarReflectorCPP2] CAM_COMP: Return new Compositor - Copy from Resources");
            return compositor;
    
        }
    } 

    // UtilityFunctions::print("[PlanarReflectorCPP2] CAM_COMP: Return new EMPTY Compositor");
    return Ref<Compositor>();
}

void PlanarReflectorCPP::update_compositor_parameters()
{
    // UtilityFunctions::print("[PlanarReflectorCPP2] update_compositor_parameters- Called");


    if (!active_compositor.is_valid()) return;
    
    TypedArray<CompositorEffect> effects = active_compositor->get_compositor_effects();
    if (effects.size() > 0) {
        CompositorEffect* effect = Object::cast_to<CompositorEffect>(effects[0]);
        if (effect) {
            double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
            effect->set("effect_enabled", hide_intersect_reflections);
            effect->set("fill_enabled", fill_reflection_experimental);
            effect->set("intersect_height", height);
            // UtilityFunctions::print("[PlanarReflectorCPP2] Updated Compositor Effect");
        }
    }
}

// Reflection Calculation Methods
Plane PlanarReflectorCPP::calculate_reflection_plane()
{
    if (!is_inside_tree()) {
        return Plane();
    }
        
    Transform3D current_transform = get_global_transform();
    
    Transform3D reflection_transform = current_transform * Transform3D().rotated(Vector3(1, 0, 0), Math_PI / 2.0);
    Vector3 plane_origin = reflection_transform.get_origin();
    Vector3 plane_normal = reflection_transform.get_basis().get_column(2).normalized();
    
    cached_reflection_plane = Plane(plane_normal, plane_origin.dot(plane_normal));
    last_global_transform = current_transform;
    
    return cached_reflection_plane;
}

void PlanarReflectorCPP::update_reflect_viewport_size()
{
    if (!reflect_viewport) {
        UtilityFunctions::print("[PlanarReflectorCPP] ERROR: update_reflect_viewport_size - reflect_viewport is null");

        return;
    }
    
    // Optimized viewport size checking frequency  
    if (frame_counter - last_viewport_check_frame < viewport_check_frequency) {
        return;
    }
    last_viewport_check_frame = frame_counter;
    
    Vector2i target_size = get_target_viewport_size();
    Camera3D *active_cam = get_active_camera();
    
    if (use_lod && active_cam) {
        target_size = apply_lod_to_size(target_size, active_cam);
    }

    reflect_viewport->set_size(target_size);
}
//This is the method that updated the Reflection Camera and Sets Shader Parameters (Refresh Method)
void PlanarReflectorCPP::set_reflection_camera_transform()
{
    // UtilityFunctions::print("[PlanarReflectorCPP] set_reflection_camera_transform called");

    if (!is_inside_tree()) {
        UtilityFunctions::print("[PlanarReflectorCPP] ERROR: set_reflection_camera_transform Stoped - Not Inside Tree");

        return;
    }
        
    Camera3D *active_camera = get_active_camera();
    if (!active_camera || !reflect_camera) {
    UtilityFunctions::print("[PlanarReflectorCPP] set_reflection_camera_transform Stoped");

        return;
    }
        
    update_camera_projection();
    Plane reflection_plane = calculate_reflection_plane();
    
    Vector3 cam_pos = active_camera->get_global_transform().get_origin();
    Vector3 proj_pos = reflection_plane.project(cam_pos);
    Vector3 mirrored_pos = cam_pos + (proj_pos - cam_pos) * 2.0;
    
    Transform3D base_reflection_transform;
    base_reflection_transform.set_origin(mirrored_pos);
    
    Basis main_basis = active_camera->get_global_transform().get_basis();
    Vector3 n = reflection_plane.get_normal();
    
    Basis reflection_basis;
    reflection_basis.set_column(0, main_basis.get_column(0).normalized().bounce(n).normalized());
    reflection_basis.set_column(1, main_basis.get_column(1).normalized().bounce(n).normalized());
    reflection_basis.set_column(2, main_basis.get_column(2).normalized().bounce(n).normalized());
    
    base_reflection_transform.set_basis(reflection_basis);
    Transform3D final_reflection_transform = apply_reflection_offset(base_reflection_transform);
    
    reflect_camera->set_global_transform(final_reflection_transform);

    // UtilityFunctions::print("[PlanarReflectorCPP] set_reflection_camera_transform ENDED");
    
    update_shader_parameters();
}

void PlanarReflectorCPP::update_shader_parameters()
{
    // UtilityFunctions::print("[PlanarReflectorCPP2] update_shader_parameters called");

    if (get_surface_override_material_count() == 0) {
        UtilityFunctions::print("[PlanarReflectorCPP] ERROR: update_shader_parameters - No surface material");

        return;
    }
    
    ShaderMaterial *material  = Object::cast_to<ShaderMaterial>(get_active_material(0).ptr()); 
    // ShaderMaterial *material = get_cached_material();
    if (!material || !reflect_viewport) {
        UtilityFunctions::print("[PlanarReflectorCPP] ERROR: update_shader_parameters - No material or ViewPort");
        return;
    }

    Ref<Texture2D> reflection_texture = reflect_viewport->get_texture();
    bool is_orthogonal = false;
    
    Camera3D *active_cam = get_active_camera();
    if (active_cam) {
        is_orthogonal = (active_cam->get_projection() == Camera3D::PROJECTION_ORTHOGONAL);
    }
    
    if(reflection_texture.is_null() || reflection_texture.is_valid() == false || reflection_texture->get_size() != reflect_viewport->get_size())
    {
        UtilityFunctions::print("[PlanarReflectorCPP] ERROR: update_shader_parameters - No valid texture found");
    }
    
    // Update Sahder parameter update logic
    // UtilityFunctions::print("[PlanarReflectorCPP2] Updating Shader Parameters - Reset Texture to Shader");
    material->set_shader_parameter("reflection_screen_texture", reflection_texture);
    material->set_shader_parameter("is_orthogonal_camera", is_orthogonal);
    material->set_shader_parameter("ortho_uv_scale", ortho_uv_scale);
    material->set_shader_parameter("reflection_offset_enabled", enable_reflection_offset);
    material->set_shader_parameter("reflection_offset_position", reflection_offset_position);
    material->set_shader_parameter("reflection_offset_scale", reflection_offset_scale);
    material->set_shader_parameter("reflection_plane_normal", cached_reflection_plane.get_normal());
    material->set_shader_parameter("reflection_plane_distance", cached_reflection_plane.d);
    material->set_shader_parameter("planar_surface_y", get_global_transform().get_origin().y);
}

void PlanarReflectorCPP::update_camera_projection()
{
    Camera3D *active_cam = get_active_camera();
    if (!active_cam || !reflect_camera) {
        return;
    }
    
    if (auto_detect_camera_mode) {
        reflect_camera->set_projection(active_cam->get_projection());
    }
    
    if (reflect_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL) {
        reflect_camera->set_size(active_cam->get_size() * ortho_scale_multiplier);
    } else {
        reflect_camera->set_fov(active_cam->get_fov());
    }
}

void PlanarReflectorCPP::clear_shader_texture_references()
{
    if (get_surface_override_material_count() == 0) {
        return;
    }

    Ref<Material> material = get_active_material(0);

    if (material.is_valid() && Object::cast_to<ShaderMaterial>(material.ptr())) 
    {
        // UtilityFunctions::print("[PlanarReflectorCPP2] Clearing Shader Texture References");
        ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(material.ptr());
        shader_material->set_shader_parameter("reflection_screen_texture", Variant());
    }
    
}

Transform3D PlanarReflectorCPP::apply_reflection_offset(const Transform3D &base_transform)
{
    if (!enable_reflection_offset) {
        return base_transform;
    }
    
    Transform3D result_transform = base_transform;
    
    switch (offset_blend_mode) {
        case 0: // Add
            result_transform.set_origin(result_transform.get_origin() + cached_offset_transform.get_origin());
            if (reflection_offset_rotation != Vector3()) {
                result_transform.set_basis(result_transform.get_basis() * cached_offset_transform.get_basis());
            }
            break;
        case 1: // Multiply
            result_transform = result_transform * cached_offset_transform;
            break;
        case 2: // Screen space shift
            if (main_camera) {
                Vector3 view_offset = main_camera->get_global_transform().get_basis().xform(cached_offset_transform.get_origin());
                result_transform.set_origin(result_transform.get_origin() + view_offset);
                result_transform.set_basis(result_transform.get_basis() * cached_offset_transform.get_basis());
            }
            break;
    }
    
    return result_transform;
}

Vector2i PlanarReflectorCPP::get_target_viewport_size()
{
    Vector2i target_size = Vector2i(1920, 1080); // Default fallback
    
    if (Engine::get_singleton()->is_editor_hint()) {
        // Editor mode
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
        // Game mode
        if (get_viewport()) {
            target_size = get_viewport()->get_visible_rect().size;
        }
    }
    
    // UtilityFunctions::print("[PlanarReflectorCPP2] Target viewport size: ", target_size);
    return target_size;
}

Vector2i PlanarReflectorCPP::apply_lod_to_size(Vector2i target_size, Camera3D *active_cam)
{
    if (!is_inside_tree()) {
        return target_size;
    }
        
    double distance = get_global_transform().get_origin().distance_to(active_cam->get_global_transform().get_origin());
    
    // Cache LOD calculations when distance hasn't changed much
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
    result_size.x = Math::max(result_size.x, 128);
    result_size.y = Math::max(result_size.y, 128);
    return result_size;
}

// Public Interface Methods - CRITICAL FOR PLUGIN HELPER
void PlanarReflectorCPP::set_editor_camera(Camera3D *viewport_camera)
{
    UtilityFunctions::print("[PlanarReflectorCPP2] set_editor_camera called");

    editor_camera = viewport_camera;
    update_reflect_viewport_size();
    set_reflection_camera_transform();
    update_compositor_parameters();
}

Camera3D* PlanarReflectorCPP::get_active_camera()
{
    if (Engine::get_singleton()->is_editor_hint()) {
        return editor_camera ? editor_camera : main_camera;
    }
    
    // Game mode - add validation
    if (!main_camera) {
        // UtilityFunctions::print("[PlanarReflectorCPP2] WARNING: main_camera is null in game mode");
        return nullptr;
    }

    return main_camera;
}

bool PlanarReflectorCPP::is_planar_reflector_active()
{
    return is_active;
}

void PlanarReflectorCPP::_exit_tree()
{
    // CRITICAL FIX: Clear shader references FIRST
    clear_shader_texture_references();
}


// Method Bindings
void PlanarReflectorCPP::_bind_methods() 
{
    // Core property
    ClassDB::bind_method(D_METHOD("set_is_active", "p_active"), &PlanarReflectorCPP::set_is_active);
    ClassDB::bind_method(D_METHOD("get_is_active"), &PlanarReflectorCPP::get_is_active);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_active"), "set_is_active", "get_is_active");

    // Core camera and resolution properties
    ClassDB::bind_method(D_METHOD("set_main_camera", "p_camera"), &PlanarReflectorCPP::set_main_camera);
    ClassDB::bind_method(D_METHOD("get_main_camera"), &PlanarReflectorCPP::get_main_camera);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "main_camera", PROPERTY_HINT_NODE_TYPE, "Camera3D"), "set_main_camera", "get_main_camera");
    
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

    // Reflection Compositor Effects Group
    ADD_GROUP("Reflection Compositor Effects", "");
    // ClassDB::bind_method(D_METHOD("set_use_custom_compositor", "p_use_custom"), &PlanarReflectorCPP2::set_use_custom_compositor);
    // ClassDB::bind_method(D_METHOD("get_use_custom_compositor"), &PlanarReflectorCPP2::get_use_custom_compositor);
    // ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_custom_compositor"), "set_use_custom_compositor", "get_use_custom_compositor");

    ClassDB::bind_method(D_METHOD("set_active_compositor", "p_compositor"), &PlanarReflectorCPP::set_active_compositor);
    ClassDB::bind_method(D_METHOD("get_active_compositor"), &PlanarReflectorCPP::get_active_compositor);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "active_compositor", PROPERTY_HINT_RESOURCE_TYPE, "Compositor"), "set_active_compositor", "get_active_compositor");

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

    // Utility methods for editor integration - CRITICAL FOR PLUGIN HELPER
    ClassDB::bind_method(D_METHOD("set_editor_camera", "viewport_camera"), &PlanarReflectorCPP::set_editor_camera);
    ClassDB::bind_method(D_METHOD("get_active_camera"), &PlanarReflectorCPP::get_active_camera);
    ClassDB::bind_method(D_METHOD("is_planar_reflector_active"), &PlanarReflectorCPP::is_planar_reflector_active);
    
    // CRITICAL: Additional methods that plugin helper needs
    ClassDB::bind_method(D_METHOD("update_reflect_viewport_size"), &PlanarReflectorCPP::update_reflect_viewport_size);
    ClassDB::bind_method(D_METHOD("set_reflection_camera_transform"), &PlanarReflectorCPP::set_reflection_camera_transform);
    
    // Method bindings for manual calls (useful for debugging and plugin integration)
    ClassDB::bind_method(D_METHOD("setup_reflection_camera_and_viewport"), &PlanarReflectorCPP::setup_reflection_camera_and_viewport);

    ClassDB::bind_method(D_METHOD("initial_setup"), &PlanarReflectorCPP::initial_setup);

    ClassDB::bind_method(D_METHOD("setup_compositor_reflection_effect", "reflect_cam"), &PlanarReflectorCPP::setup_compositor_reflection_effect);

    ClassDB::bind_method(D_METHOD("create_viewport_deferred"), &PlanarReflectorCPP::create_viewport_deferred);
    ClassDB::bind_method(D_METHOD("clear_shader_texture_references"), &PlanarReflectorCPP::clear_shader_texture_references);
    ClassDB::bind_method(D_METHOD("finalize_setup"), &PlanarReflectorCPP::finalize_setup);
}

// SETTERS AND GETTERS IMPLEMENTATION
void PlanarReflectorCPP::set_is_active(const bool p_active) { is_active = p_active; }
bool PlanarReflectorCPP::get_is_active() const { return is_active; }

void PlanarReflectorCPP::set_main_camera(Camera3D *p_camera) 
{
    main_camera = Object::cast_to<Camera3D>(p_camera);
    
    if (reflect_camera && main_camera) {
        reflect_camera->set_attributes(main_camera->get_attributes());
        reflect_camera->set_doppler_tracking(main_camera->get_doppler_tracking());
        setup_reflection_environment();
    }
}
Camera3D* PlanarReflectorCPP::get_main_camera() const { return main_camera; }

void PlanarReflectorCPP::set_reflection_camera_resolution(const Vector2i p_resolution) 
{ 
    reflection_camera_resolution = p_resolution;
    if (reflect_viewport) {
        reflect_viewport->set_size(reflection_camera_resolution);
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
    if (reflect_camera) {
        int cull_mask = reflection_layers;
        reflect_camera->set_cull_mask(cull_mask);
        is_layer_one_active = bool(cull_mask & (1 << 0));
    }
}
int PlanarReflectorCPP::get_reflection_layers() const { return reflection_layers; }

void PlanarReflectorCPP::set_use_custom_environment(bool p_use_custom)
{
    use_custom_environment = p_use_custom;
    if (is_inside_tree()) {
        setup_reflection_environment();
    }
}
bool PlanarReflectorCPP::get_use_custom_environment() const { return use_custom_environment; }

void PlanarReflectorCPP::set_custom_environment(Environment *p_environment)
{
    custom_environment = Object::cast_to<Environment>(p_environment);
    if (use_custom_environment && is_inside_tree()) {
        setup_reflection_environment();
    }
}
Environment* PlanarReflectorCPP::get_custom_environment() const { return custom_environment; }

// Reflection Compositor Effects Group
void PlanarReflectorCPP::set_active_compositor(Compositor *p_compositor)
{
    if (p_compositor) {
        active_compositor = Ref<Compositor>(p_compositor);
    } else {
        active_compositor.unref();  // Properly clear the Ref
    }
    
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
    // UtilityFunctions::print("[PlanarReflectorCPP2] Hide Intersect Reflections - Set Started");
    hide_intersect_reflections = p_hide;
    if (reflect_camera && is_inside_tree()) {
        // setup_compositor_reflection_effect(reflect_camera);
        update_compositor_parameters();
    }
}
bool PlanarReflectorCPP::get_hide_intersect_reflections() const { return hide_intersect_reflections; }

void PlanarReflectorCPP::set_override_YAxis_height(bool p_override)
{
    override_YAxis_height = p_override;
    if (reflect_camera && is_inside_tree()) {
        // setup_compositor_reflection_effect(reflect_camera);
        update_compositor_parameters();
    }
}
bool PlanarReflectorCPP::get_override_YAxis_height() const { return override_YAxis_height; }

void PlanarReflectorCPP::set_new_YAxis_height(double p_height)
{
    new_YAxis_height = p_height;
    if (reflect_camera && is_inside_tree()) {
        // setup_compositor_reflection_effect(reflect_camera);
        update_compositor_parameters();
    }
}
double PlanarReflectorCPP::get_new_YAxis_height() const { return new_YAxis_height; }

void PlanarReflectorCPP::set_fill_reflection_experimental(bool p_fill)
{
    fill_reflection_experimental = p_fill;
    if (reflect_camera && is_inside_tree()) {
        // setup_compositor_reflection_effect(reflect_camera);
        update_compositor_parameters();
    }
}
bool PlanarReflectorCPP::get_fill_reflection_experimental() const { return fill_reflection_experimental; }

// Reflection Offset Control Group
void PlanarReflectorCPP::set_enable_reflection_offset(bool p_enable)
{
    enable_reflection_offset = p_enable;
}
bool PlanarReflectorCPP::get_enable_reflection_offset() const { return enable_reflection_offset; }

void PlanarReflectorCPP::set_reflection_offset_position(const Vector3 &p_position)
{
    reflection_offset_position = p_position;
}
Vector3 PlanarReflectorCPP::get_reflection_offset_position() const { return reflection_offset_position; }

void PlanarReflectorCPP::set_reflection_offset_rotation(const Vector3 &p_rotation)
{
    reflection_offset_rotation = p_rotation;
}
Vector3 PlanarReflectorCPP::get_reflection_offset_rotation() const { return reflection_offset_rotation; }

void PlanarReflectorCPP::set_reflection_offset_scale(double p_scale)
{
    reflection_offset_scale = p_scale;
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