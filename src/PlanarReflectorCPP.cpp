#include "PlanarReflectorCPP.h" //Must have reference to our header .h file
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
#include <godot_cpp/core/math.hpp>
#include <iostream>

using namespace godot;

//Must have _bind_methods is where you add your methods and variables you want Godot to know about

//Define your constructor (Must have)
PlanarReflectorCPP::PlanarReflectorCPP() 
{  

    // Initialize default values
    is_active = true;
    reflection_camera_resolution = Vector2i(1920, 1080);
    ortho_scale_multiplier = 1.0;
    ortho_uv_scale = 1.0;
    auto_detect_camera_mode = true;
    reflection_layers = 1;
    use_custom_environment = true;
    enable_reflection_offset = false;
    reflection_offset_position = Vector3(0.0, 0.0, 0.0);
    reflection_offset_rotation = Vector3(0.0, 0.0, 0.0);
    reflection_offset_scale = 1.0;
    offset_blend_mode = 0;
    update_frequency = 1;
    use_lod = true;
    lod_distance_near = 10.0;
    lod_distance_far = 30.0;
    lod_resolution_multiplier = 0.45;
    
    // Initialize internal variables
    frame_counter = 0;
    position_threshold = 0.01;
    rotation_threshold = 0.001;
    is_layer_one_active = true;

    // UtilityFunctions::print("PlanarReflectorCPP Constructor ready");
}

//Define your destructor (Must have)
PlanarReflectorCPP::~PlanarReflectorCPP() 
{
    // Cleanup is handled by Godot's garbage collection for our node references
}

//Define the implementation of the functions you have in the header file from here
void PlanarReflectorCPP::_process(double delta) 
{
    if(!is_active) return;

    if (!main_camera || !reflect_camera || !reflect_viewport) {
        return;
    }

    frame_counter++;
    
    // Update offset cache if needed
    update_offset_cache();
    
    // Skip updates based on frequency and movement threshold
    bool should_update = (frame_counter % update_frequency == 0);
    
    if (should_update || Engine::get_singleton()->is_editor_hint()) {
        if (should_update_reflection()) {
            update_viewport();
            update_reflection_camera();
        }
    }
    
    // Force updates in editor to see changes immediately
    if (Engine::get_singleton()->is_editor_hint()) {
        update_reflection_camera();
    }
}

void PlanarReflectorCPP::_ready() 
{
    // Setup the reflection system similar to GDScript version
    setup_reflection_viewport();
    setup_reflection_camera();
    setup_reflection_layers();
    setup_reflection_environment();
    
    // Initialize offset cache
    update_offset_cache();

     // Try to find editor helper if in editor
    find_editor_helper();
    
    //UtilityFunctions::print("PlanarReflectorCPP ready completed");
}

void PlanarReflectorCPP::setup_reflection_viewport()
{
    // Create SubViewport similar to GDScript version
    reflect_viewport = memnew(SubViewport);
    add_child(reflect_viewport);
    
    reflect_viewport->set_size(reflection_camera_resolution);
    reflect_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
    reflect_viewport->set_msaa_3d(Viewport::MSAA_4X);
    reflect_viewport->set_positional_shadow_atlas_size(2048);
    reflect_viewport->set_use_own_world_3d(false);
}

void PlanarReflectorCPP::setup_reflection_camera()
{
    if (!reflect_viewport) {
        //UtilityFunctions::printerr("Reflect viewport not created yet!");
        return;
    }

    // Create reflection camera
    reflect_camera = memnew(Camera3D);
    reflect_viewport->add_child(reflect_camera);
    
    if (main_camera) {
        // Copy basic camera properties from main camera
        reflect_camera->set_attributes(main_camera->get_attributes());
        reflect_camera->set_doppler_tracking(main_camera->get_doppler_tracking());
    }
    
    reflect_camera->set_current(true);
}

void PlanarReflectorCPP::setup_reflection_layers()
{
    if (!reflect_camera) {
        return;
    }
    
    // Configure which layers the reflection camera can see
    reflect_camera->set_cull_mask(reflection_layers);
    
    // Check if layer 1 is active (bit 0 = layer 1)
    if (reflection_layers & (1 << (1 - 1))) {
        is_layer_one_active = true;
    } else {
        is_layer_one_active = false;
        UtilityFunctions::print("Layer 1 not active, make sure to add the layers to the scene Lights cull masks");
    }
}

void PlanarReflectorCPP::setup_reflection_environment()
{
    if (!reflect_camera || !main_camera) {
        return;
    }
    
    // Set up environment for reflection camera
    if (use_custom_environment) {
        Environment *reflection_env = memnew(Environment);
        
        if (custom_environment) {
            // Use the custom environment provided
            reflection_env = custom_environment;
        } else {
            // Auto-generate clean environment
            reflection_env->set_background(Environment::BG_CLEAR_COLOR);
            
            // Copy lighting settings from main environment but exclude sky contribution
            Ref<Environment> main_env = main_camera->get_environment();
            if (main_env.is_valid()) {
                // Copy ambient light but not from sky
                if (main_env->get_ambient_source() == Environment::AMBIENT_SOURCE_SKY) {
                    reflection_env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
                    reflection_env->set_ambient_light_color(Color(0.4, 0.4, 0.4)); // Neutral gray
                    reflection_env->set_ambient_light_energy(0.3);
                } else {
                    reflection_env->set_ambient_source(main_env->get_ambient_source());
                    reflection_env->set_ambient_light_color(main_env->get_ambient_light_color());
                    reflection_env->set_ambient_light_energy(main_env->get_ambient_light_energy());
                }
                
                // Disable fog to avoid sky color bleeding
                reflection_env->set_fog_enabled(false);
            }
        }
        
        reflect_camera->set_environment(reflection_env);
    } else {
        // Use main camera environment as-is
        reflect_camera->set_environment(main_camera->get_environment());
    }
}

Plane PlanarReflectorCPP::calculate_reflection_plane()
{
    // Calculate the reflection plane with optional offset and perturbation
    Transform3D reflection_transform = get_global_transform() * Transform3D().rotated(Vector3(1, 0, 0), Math_PI / 2.0);
    Vector3 plane_origin = reflection_transform.get_origin();
    Vector3 plane_normal = reflection_transform.get_basis().get_column(2).normalized();
    return Plane(plane_normal, plane_origin.dot(plane_normal));
}

void PlanarReflectorCPP::update_reflection_camera()
{
    Camera3D* active_cam = get_active_camera();
    if (!active_cam || !reflect_camera) {
        return;
    }

    // Update camera projection based on main camera
    update_camera_projection();

    // Calculate reflection plane (with advanced features)
    Plane reflection_plane = calculate_reflection_plane();
    cached_reflection_plane = reflection_plane;
    
    Vector3 cam_pos = active_cam->get_global_transform().get_origin();
    
    Vector3 proj_pos = reflection_plane.project(cam_pos);
    Vector3 mirrored_pos = cam_pos + (proj_pos - cam_pos) * 2.0;
    
    // Create base reflection transform
    Transform3D base_reflection_transform;
    base_reflection_transform.set_origin(mirrored_pos);
    
    Basis main_basis = active_cam->get_global_transform().get_basis();
    Vector3 normal = reflection_plane.get_normal();
    
    Basis reflection_basis;
    reflection_basis.set_column(0, main_basis.get_column(0).normalized().bounce(normal).normalized());
    reflection_basis.set_column(1, main_basis.get_column(1).normalized().bounce(normal).normalized());
    reflection_basis.set_column(2, main_basis.get_column(2).normalized().bounce(normal).normalized());
    
    base_reflection_transform.set_basis(reflection_basis);
    
    // Apply reflection offset
    Transform3D final_reflection_transform = apply_reflection_offset(base_reflection_transform);
    
    // Set the final transform
    reflect_camera->set_global_transform(final_reflection_transform);
    
    // Pass parameters to shader
    update_shader_parameters();
}

void PlanarReflectorCPP::update_camera_projection()
{
    if (!main_camera || !reflect_camera) {
        return;
    }

    if (auto_detect_camera_mode) {
        reflect_camera->set_projection(main_camera->get_projection());
        if (Engine::get_singleton()->is_editor_hint()) { // Force perspective in editor
            reflect_camera->set_projection(Camera3D::PROJECTION_PERSPECTIVE);
        }
    }
    
    if (reflect_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL) {
        reflect_camera->set_size(main_camera->get_size() * ortho_scale_multiplier);
    } else {
        reflect_camera->set_fov(main_camera->get_fov());
    }
}

void PlanarReflectorCPP::update_viewport() {
    if (!reflect_viewport) {
        return;
    }

    Vector2i target_size;
    
    if (Engine::get_singleton()->is_editor_hint() && editor_helper) {
        // Get editor viewport size from helper
        Variant size_var = editor_helper->call("get_editor_viewport_size");
        if (size_var.get_type() == Variant::VECTOR2I) {
            target_size = size_var;
        } else {
            // Fallback
            target_size = get_viewport()->get_visible_rect().size;
        }
    } else {
        // Game mode - use regular viewport
        target_size = get_viewport()->get_visible_rect().size;
    }
    
    // Apply LOD based on distance
    if (use_lod && main_camera) {
        Camera3D* active_cam = get_active_camera();
        double distance = get_global_transform().get_origin().distance_to(
            active_cam->get_global_transform().get_origin()
        );
        double lod_factor = 1.0;
        
        if (distance > lod_distance_near) {
            double lerp_factor = Math::clamp(
                (distance - lod_distance_near) / (lod_distance_far - lod_distance_near), 
                0.0, 1.0
            );
            lod_factor = Math::lerp(1.0, lod_resolution_multiplier, lerp_factor);
        }
        
        target_size = Vector2i((double)target_size.x * lod_factor, (double)target_size.y * lod_factor);
        target_size.x = Math::max(target_size.x, 128);
        target_size.y = Math::max(target_size.y, 128);
    }
    
    reflect_viewport->set_size(target_size);
}

void PlanarReflectorCPP::update_shader_parameters()
{
    if (!reflect_viewport) {
        return;
    }

    // Update all shader parameters including advanced features
    if (active_shader_material == nullptr) {
        Ref<Material> mat = get_active_material(0);
        active_shader_material = Object::cast_to<ShaderMaterial>(mat.ptr());
    }
    
    ShaderMaterial *material = active_shader_material;

    if (material == nullptr) {
        UtilityFunctions::printerr("Material not found or is not shader material");
        return;
    }
    
    // Core reflection texture
    material->set_shader_parameter("reflection_screen_texture", reflect_viewport->get_texture());
    
    // Camera mode detection
    bool is_orthogonal = false;
    if (Engine::get_singleton()->is_editor_hint()) { // Force perspective in editor
        is_orthogonal = false;
    } else if (main_camera) {
        is_orthogonal = (main_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL);
    }

    material->set_shader_parameter("is_orthogonal_camera", is_orthogonal);
    material->set_shader_parameter("ortho_uv_scale", ortho_uv_scale);
    
    // Pass reflection offset info to shader for additional effects
    material->set_shader_parameter("reflection_offset_enabled", enable_reflection_offset);
    material->set_shader_parameter("reflection_offset_position", reflection_offset_position);
    material->set_shader_parameter("reflection_offset_scale", reflection_offset_scale);
    material->set_shader_parameter("reflection_plane_normal", cached_reflection_plane.get_normal());
    material->set_shader_parameter("reflection_plane_distance", cached_reflection_plane.d);
    material->set_shader_parameter("planar_surface_y", get_global_transform().get_origin().y);
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
            if (main_camera) {
                Vector3 view_offset = main_camera->get_global_transform().get_basis().xform(cached_offset_transform.get_origin());
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
    
    // Check if offset parameters changed
    if (last_offset_position != reflection_offset_position || 
        last_offset_rotation != reflection_offset_rotation) {
        
        // Create offset transform
        Basis offset_basis;
        offset_basis = offset_basis.rotated(Vector3(1, 0, 0), Math::deg_to_rad(reflection_offset_rotation.x));
        offset_basis = offset_basis.rotated(Vector3(0, 1, 0), Math::deg_to_rad(reflection_offset_rotation.y));
        offset_basis = offset_basis.rotated(Vector3(0, 0, 1), Math::deg_to_rad(reflection_offset_rotation.z));
        
        cached_offset_transform = Transform3D(offset_basis, reflection_offset_position * reflection_offset_scale);
        
        last_offset_position = reflection_offset_position;
        last_offset_rotation = reflection_offset_rotation;
    }
}

//CODE REQUIRED TO USE THE EDITOR PLUGIN
#pragma region // EDITOR PLUGIN HELPER FUNCTIONS
void PlanarReflectorCPP::find_editor_helper() {
    if (!Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    
    // Try to find the editor helper singleton
    if (Engine::get_singleton()->has_singleton("PlanarReflectorEditorHelper")) {
        editor_helper = Engine::get_singleton()->get_singleton("PlanarReflectorEditorHelper");
        if (editor_helper) {
            UtilityFunctions::print("PlanarReflectorCPP: Found editor helper");
        }
    }
}


Camera3D* PlanarReflectorCPP::get_active_camera() {
    if (Engine::get_singleton()->is_editor_hint() && editor_helper) {
        // Try to get editor camera from helper
        Variant camera_var = editor_helper->call("get_editor_camera");
        if (camera_var.get_type() == Variant::OBJECT) {
            Object* cam_obj = camera_var;
            if (cam_obj) {
                Camera3D* ed_cam = Object::cast_to<Camera3D>(cam_obj);
                if (ed_cam && ed_cam->is_inside_tree()) {
                    return ed_cam;
                }
            }
        }
    }
    
    // Fallback to main camera
    return main_camera;
}

Viewport* PlanarReflectorCPP::get_active_viewport() {
    Camera3D* active_cam = get_active_camera();
    if (active_cam) {
        return active_cam->get_viewport();
    }
    return get_viewport();
}


#pragma endregion


bool PlanarReflectorCPP::should_update_reflection()
{
    if (!main_camera) {
        return false;
    }

    Vector3 current_pos = main_camera->get_global_transform().get_origin();
    Basis current_basis = main_camera->get_global_transform().get_basis();
    
    // Check if camera moved/rotated enough to warrant update
    if (last_camera_position != Vector3()) {
        double pos_diff = current_pos.distance_to(last_camera_position);
        double rot_diff = current_basis.get_euler().distance_to(last_camera_rotation.get_euler());
        
        if (pos_diff < position_threshold && rot_diff < rotation_threshold) {
            return false; // Skip update if camera barely moved
        }
    }
    
    last_camera_position = current_pos;
    last_camera_rotation = current_basis;
    
    return true;
}

//METHOD BINDINGS AND SETTERS FROM HERE
void PlanarReflectorCPP::_bind_methods() 
{
    //Method bindings to be able to access those from GDScript and make Godot "See those"
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
}

// SETTERS AND GETTERS IMPLEMENTATION FROM HERE:

void PlanarReflectorCPP::set_is_active(const bool p_active) { is_active = p_active; };
bool PlanarReflectorCPP::get_is_active() const { return is_active; };

// Core camera and resolution controls
void PlanarReflectorCPP::set_main_camera(Camera3D *p_camera) 
{
    main_camera = Object::cast_to<Camera3D>(p_camera);
    
    // Update reflection camera properties if both cameras exist
    if (main_camera && reflect_camera) {
        reflect_camera->set_attributes(main_camera->get_attributes());
        reflect_camera->set_doppler_tracking(main_camera->get_doppler_tracking());
        setup_reflection_environment();
    }
}

Camera3D* PlanarReflectorCPP::get_main_camera() const {return main_camera;}
void PlanarReflectorCPP::set_reflection_camera_resolution(const Vector2i p_resolution) 
{ 
    reflection_camera_resolution = p_resolution;
    
    // Update viewport size if it exists
    if (reflect_viewport) {
        reflect_viewport->set_size(reflection_camera_resolution);
    }
}

Vector2i PlanarReflectorCPP::get_reflection_camera_resolution() const {return reflection_camera_resolution;}
// Camera Controls Group
void PlanarReflectorCPP::set_ortho_scale_multiplier(double p_multiplier) {ortho_scale_multiplier = p_multiplier;}
double PlanarReflectorCPP::get_ortho_scale_multiplier() const {return ortho_scale_multiplier;}

void PlanarReflectorCPP::set_ortho_uv_scale(double p_scale) {ortho_uv_scale = p_scale;}
double PlanarReflectorCPP::get_ortho_uv_scale() const {return ortho_uv_scale;}

void PlanarReflectorCPP::set_auto_detect_camera_mode(bool p_auto_detect){auto_detect_camera_mode = p_auto_detect;}
bool PlanarReflectorCPP::get_auto_detect_camera_mode() const{return auto_detect_camera_mode;}
// Reflection Layers and Environment Group
void PlanarReflectorCPP::set_reflection_layers(int p_layers)
{
    reflection_layers = p_layers;    
    // Update reflection camera cull mask if it exists
    if (reflect_camera) {
        setup_reflection_layers();
    }
}
int PlanarReflectorCPP::get_reflection_layers() const{return reflection_layers;}

void PlanarReflectorCPP::set_use_custom_environment(bool p_use_custom)
{
    use_custom_environment = p_use_custom;
    // Update environment setup if reflection camera exists
    if (reflect_camera) {
        setup_reflection_environment();
    }
}

bool PlanarReflectorCPP::get_use_custom_environment() const{return use_custom_environment;}
void PlanarReflectorCPP::set_custom_environment(Environment *p_environment)
{
    custom_environment = Object::cast_to<Environment>(p_environment);
    // Update environment setup if reflection camera exists and we're using custom environment
    if (reflect_camera && use_custom_environment) {
        setup_reflection_environment();
    }
}
Environment* PlanarReflectorCPP::get_custom_environment() const{return custom_environment;}

// Reflection Offset Control Group
void PlanarReflectorCPP::set_enable_reflection_offset(bool p_enable)
{
    enable_reflection_offset = p_enable;
    update_offset_cache();
}
bool PlanarReflectorCPP::get_enable_reflection_offset() const{return enable_reflection_offset;}

void PlanarReflectorCPP::set_reflection_offset_position(const Vector3 &p_position)
{
    reflection_offset_position = p_position;
    update_offset_cache();
}
Vector3 PlanarReflectorCPP::get_reflection_offset_position() const{ return reflection_offset_position;}

void PlanarReflectorCPP::set_reflection_offset_rotation(const Vector3 &p_rotation)
{
    reflection_offset_rotation = p_rotation;
    update_offset_cache();
}
Vector3 PlanarReflectorCPP::get_reflection_offset_rotation() const{return reflection_offset_rotation;}

void PlanarReflectorCPP::set_reflection_offset_scale(double p_scale)
{
    reflection_offset_scale = p_scale;
    update_offset_cache();
}
double PlanarReflectorCPP::get_reflection_offset_scale() const{return reflection_offset_scale;}

void PlanarReflectorCPP::set_offset_blend_mode(int p_mode){offset_blend_mode = Math::clamp(p_mode, 0, 2);}
int PlanarReflectorCPP::get_offset_blend_mode() const{return offset_blend_mode;}
// Performance Controls Group
void PlanarReflectorCPP::set_update_frequency(int p_frequency){update_frequency = Math::max(p_frequency, 1);}
int PlanarReflectorCPP::get_update_frequency() const{return update_frequency;}

void PlanarReflectorCPP::set_use_lod(bool p_use_lod){use_lod = p_use_lod;}
bool PlanarReflectorCPP::get_use_lod() const{return use_lod;}

void PlanarReflectorCPP::set_lod_distance_near(double p_distance){lod_distance_near = p_distance;}
double PlanarReflectorCPP::get_lod_distance_near() const{return lod_distance_near;}

void PlanarReflectorCPP::set_lod_distance_far(double p_distance){lod_distance_far = p_distance;}
double PlanarReflectorCPP::get_lod_distance_far() const{return lod_distance_far;}

void PlanarReflectorCPP::set_lod_resolution_multiplier(double p_multiplier){lod_resolution_multiplier = p_multiplier;}
double PlanarReflectorCPP::get_lod_resolution_multiplier() const{return lod_resolution_multiplier;}