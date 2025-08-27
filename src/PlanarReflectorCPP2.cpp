//PlanarReflectorCPP2.cpp FILE - SIMPLIFIED VIEWPORT MANAGEMENT
#include "PlanarReflectorCPP2.h"

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

PlanarReflectorCPP2::PlanarReflectorCPP2() 
{  
    UtilityFunctions::print("[PlanarReflectorCPP2] Constructor Started");

    // Initialize with safe defaults
    is_active = true;
    reflection_camera_resolution = Vector2i(1920, 1080);
    ortho_scale_multiplier = 1.0;
    ortho_uv_scale = 1.0;
    auto_detect_camera_mode = true;
    reflection_layers = 1;
    use_custom_environment = false;
    
    // Compositor Effects initialization
    // use_custom_compositor = false;
    active_compositor = nullptr;
    hide_intersect_reflections = true;
    override_YAxis_height = false;
    new_YAxis_height = 0.0;
    fill_reflection_experimental = false;
    
    enable_reflection_offset = false;
    reflection_offset_position = Vector3(0.0, 0.0, 0.0);
    reflection_offset_rotation = Vector3(0.0, 0.0, 0.0);
    reflection_offset_scale = 1.0;
    offset_blend_mode = 0;
    
    update_frequency = 2;
    use_lod = true;
    lod_distance_near = 8.0;
    lod_distance_far = 24.0;
    lod_resolution_multiplier = 0.45;
    
    // Initialize internal variables
    frame_counter = 0;
    position_threshold = 0.01;
    rotation_threshold = 0.001;
    is_layer_one_active = true;
    
    // Initialize performance caches
    material_cache_valid = false;
    cached_material_pointer = nullptr;
    last_viewport_check_frame = -1;
    viewport_check_frequency = 5;
    reflection_plane_cache_valid = false;
    last_distance_check = -1.0;
    cached_lod_factor = 1.0;
}

PlanarReflectorCPP2::~PlanarReflectorCPP2() 
{
    // Cleanup handled by _exit_tree
    UtilityFunctions::print("[PlanarReflectorCPP2] DEStructor Started");

}

void PlanarReflectorCPP2::_ready() 
{
    UtilityFunctions::print("[PlanarReflectorCPP2] Ready Started");

    add_to_group("planar_reflectors");
    // IMMEDIATE setup instead of deferred to avoid binding issues
    initial_setup();
}

void PlanarReflectorCPP2::_notification(int what)
{
    if (what == NOTIFICATION_TRANSFORM_CHANGED) {
        reflection_plane_cache_valid = false;
        if (reflect_camera && reflect_camera->get_compositor().is_valid()) 
        {
            //TODO: IMPLEMENT THIS BACK IN A NEW WAY # DEBUG
            // Ref<Compositor> compositor = reflect_camera->get_compositor();
            // CompositorEffect *effect = get_reflection_effect(compositor.ptr());
            // if (effect) {
            //     set_reflection_effect(effect);
            // }
        }
    }
}

void PlanarReflectorCPP2::initial_setup()
{
    UtilityFunctions::print("[PlanarReflectorCPP2] Initial Setup started");
    find_editor_helper();
    // find_reflection_effect_class();
    setup_reflection_camera_and_viewport();
    update_offset_cache();
    invalidate_all_caches();
    update_reflect_viewport_size();
    set_reflection_camera_transform();
    UtilityFunctions::print("[PlanarReflectorCPP2] Initial Setup Completed");

}

void PlanarReflectorCPP2::_process(double delta) 
{
    if (!is_inside_tree() || !is_active) {
        return;
    }
    
    frame_counter++;
    update_offset_cache();
    
    // Less frequent viewport size updates 
    if (viewport_check_frequency > 0 && frame_counter % viewport_check_frequency == 0) {
        update_reflect_viewport_size();
    }
    
    bool should_update = (update_frequency > 0 && frame_counter % update_frequency == 0);
    if (should_update) {
        Camera3D *active_cam = get_active_camera();
        if (active_cam && should_update_reflection(active_cam)) {
            set_reflection_camera_transform();
        }
    }
}

void PlanarReflectorCPP2::setup_reflection_camera_and_viewport()
{
    UtilityFunctions::print("[PlanarReflectorCPP2] setup_reflection_camera_and_viewport called");

    // SIMPLIFIED SINGLE VIEWPORT APPROACH - like GDScript
    if (reflect_viewport && reflect_viewport->is_inside_tree()) {
        reflect_viewport->queue_free();
        reflect_viewport = nullptr;
    }

    reflect_viewport = memnew(SubViewport);
    reflect_viewport->set_name("ReflectionViewPort");
    add_child(reflect_viewport);
    
    reflect_viewport->set_size(reflection_camera_resolution);
    reflect_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
    reflect_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);
    reflect_viewport->set_positional_shadow_atlas_size(2048);
    reflect_viewport->set_use_own_world_3d(false);
    reflect_viewport->set_transparent_background(true);
    reflect_viewport->set_handle_input_locally(false);

    // Setup the reflection camera
    if (reflect_camera && reflect_camera->is_inside_tree()) {
        reflect_camera->queue_free();
        reflect_camera = nullptr;
    }
    
    reflect_camera = memnew(Camera3D);
    reflect_viewport->add_child(reflect_camera);
    
    // Setup the reflection camera cull mask / layers
    int cull_mask = reflection_layers;
    reflect_camera->set_cull_mask(cull_mask);
    is_layer_one_active = bool(cull_mask & (1 << 0));
    
    if (!is_layer_one_active) {
        UtilityFunctions::print("Layer 1 not active, make sure to add the layers to the scene Lights cull masks");
    }
    
    // Copy main camera properties to reflection camera
    if (main_camera) {
        reflect_camera->set_attributes(main_camera->get_attributes());
        reflect_camera->set_doppler_tracking(main_camera->get_doppler_tracking());
    }
    
    reflect_camera->set_current(true);

    // Setup environment and compositor
    setup_reflection_environment();
    
    if (hide_intersect_reflections) {
        setup_compositor_reflection_effect(reflect_camera);
    }
}

void PlanarReflectorCPP2::setup_reflection_environment()
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

void PlanarReflectorCPP2::find_editor_helper()
{
    UtilityFunctions::print("[PlanarReflectorCPP2] find_editor_helper called");

    if (Engine::get_singleton()->is_editor_hint()) {
        if (Engine::get_singleton()->has_singleton("PlanarReflectorEditorHelper")) {
            editor_helper = Engine::get_singleton()->get_singleton("PlanarReflectorEditorHelper");
        }
    }
}

//TODO: //DEBUG - REMOVE ALL REFEFRENCES TO find_reflection_effect_class IN HEADER AND CPP FILES
void PlanarReflectorCPP2::find_reflection_effect_class()
{
    // if (Engine::get_singleton()->is_editor_hint()) {
    //     if (Engine::get_singleton()->has_singleton("ReflectEffectPrePassGD")) {
    //         reflect_effect_gdscript = Engine::get_singleton()->get_singleton("ReflectEffectPrePassGD");
    //         UtilityFunctions::print("Found ReflectEffectPrePassGD class in editor");
    //     }
    //     else {
    //         reflect_effect_gdscript = nullptr;
    //         UtilityFunctions::print("NOT FOUND ReflectEffectPrePassGD class in editor");
    //     }
    // }
}

// Compositor Methods
void PlanarReflectorCPP2::setup_compositor_reflection_effect(Camera3D *reflect_cam) 
{
    if (!reflect_cam) {
        UtilityFunctions::print("[PlanarReflectorCPP2] setup_compositor_reflection_effect: Invalid reflect camera");
        return;
    }

    // Compositor *temp_compositor = nullptr;

    //Get custom compositor or create new one via editor helper
    // if (use_custom_compositor && custom_compositor) 
    if (active_compositor && active_compositor->get_compositor_effects().size() > 0)
    {
        //  temp_compositor = Object::cast_to<Compositor>(active_compositor);
         reflect_cam->set_compositor(active_compositor);
         UtilityFunctions::print("[PlanarReflectorCPP2] Custom compositor set");
    }
    else //TODO: improvement, avoid creating new compositor every time // we can cache it or just check if there is one already of a certain type //debug 
    {
        Compositor *new_compositor = nullptr;
        if(editor_helper->has_method("create_new_reflection_compositor"))
        {
            UtilityFunctions::print("[PlanarReflectorCPP2] Trying to call create_new_reflection_compositor");
            Variant result = editor_helper->call("create_new_reflection_compositor");
            new_compositor = Object::cast_to<Compositor>(result);

            if (new_compositor)
            {
                active_compositor = new_compositor;
                reflect_cam->set_compositor(active_compositor);
            }
        }
        else
        {
            UtilityFunctions::print("[PlanarReflectorCPP2] Method create_new_reflection_compositor does NOT exist");
            return;
        }

        // temp_compositor = new_compositor;
        // if(temp_compositor){
        //     UtilityFunctions::print("[PlanarReflectorCPP2] New Compositor created");
        // }
    }
    
    //Set new compositor
    



    //Get updated parameters
    godot::Dictionary new_params;
    double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
    new_params["effect_enabled"] = true;
    new_params["intersect_height"] = height;
    new_params["fill_enabled"] = fill_reflection_experimental;

    //TEST //DEBUG -TRYING TO SET THE PARAMS
    TypedArray<CompositorEffect> active_effects = active_compositor->get_compositor_effects();
    if(active_effects.size() > 0)
    {
        UtilityFunctions::print("[PlanarReflectorCPP2] active_effects is greater than 0");

        CompositorEffect* firs_effect = Object::cast_to<CompositorEffect>(active_effects[0]);
        if (firs_effect) {
            firs_effect->set("effect_enabled", hide_intersect_reflections);
            firs_effect->set("intersect_height", height);
            firs_effect->set("fill_enabled", fill_reflection_experimental);
            UtilityFunctions::print("[PlanarReflectorCPP2] firs_effect found and updated");
        }
    }
    else
    {
        UtilityFunctions::print("[PlanarReflectorCPP2] active_effects is empty");
    }
    //TODO - ADD ROUTE if we have no active effects


    //Update effect parameters via editor helper
    if(editor_helper)
    {
        if(editor_helper->has_method("update_reflection_compositor_effects2"))
        {
            UtilityFunctions::print("[PlanarReflectorCPP2] Trying to call update_reflection_compositor_effects2");
            Variant result = editor_helper->call("update_reflection_compositor_effects2", "testParam");

        }

        if(editor_helper->has_method("update_reflection_compositor_effects3"))
        {
            UtilityFunctions::print("[PlanarReflectorCPP2] Trying to call update_reflection_compositor_effects3");
            Variant result = editor_helper->call("update_reflection_compositor_effects3", reflect_cam);
        }
                
        if(editor_helper->has_method("update_reflection_compositor_effects4"))
        {
            UtilityFunctions::print("[PlanarReflectorCPP2] Trying to call update_reflection_compositor_effects4");
            Variant result = editor_helper->call("update_reflection_compositor_effects4", new_params);

        }
        
        // UtilityFunctions::print("[PlanarReflectorCPP2] Method does NOT exist");
        // UtilityFunctions::print("[PlanarReflectorCPP2] update_reflection_compositor_effects returned: ", result);

        // // Variant camera_variant = Variant(reflect_cam);
        // // editor_helper->call("update_reflection_compositor_effects", camera_variant, new_params);

        // // Variant call_result = editor_helper->call("update_reflection_compositor_effects", reflect_cam, new_params);
        // // UtilityFunctions::print("[PlanarReflectorCPP2] setup_compositor_reflection_effect: update_reflection_compositor_effects returned NIL, possible method failure");
        
    }

    
}



// void PlanarReflectorCPP2::setup_compositor_reflection_effect(Camera3D *reflect_cam) {
//     if (!reflect_cam) {
//         UtilityFunctions::print("[PlanarReflectorCPP2] setup_compositor_reflection_effect: Invalid reflect camera");
//         return;
//     }

//     if (use_custom_compositor && custom_compositor) {
//         reflect_cam->set_compositor(custom_compositor);
//         TypedArray<CompositorEffect> effects = custom_compositor->get_compositor_effects();
//         if (effects.size() > 0) {
//             CompositorEffect *active_effect = Object::cast_to<CompositorEffect>(effects[0]);
//             if (active_effect && active_effect->is_class("ReflectEffectPrePassGD")) {
//                 UtilityFunctions::print("[PlanarReflectorCPP2] active_effect is ReflectEffectPrePassGD 1 - Trying to call set_reflection_effect(active_effect);");
//                 set_reflection_effect(active_effect);
//             } else {
//                 UtilityFunctions::print("[PlanarReflectorCPP2] setup_compositor_reflection_effect: First effect is not ReflectEffectPrePassGD");
//             }
//         }
//     } else {
//         Ref<Compositor> compositor = reflect_cam->get_compositor();
//         if (!compositor.is_valid()) {
//             create_new_compositor_effect(reflect_cam);
//         } else {
//             TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
//             if (effects.size() > 0) {
//                 CompositorEffect *active_effect = Object::cast_to<CompositorEffect>(effects[0]);
//                 if (active_effect && active_effect->is_class("ReflectEffectPrePassGD")) {
//                     UtilityFunctions::print("[PlanarReflectorCPP2] active_effect is ReflectEffectPrePassGD 2 - Trying to call set_reflection_effect(active_effect);");
//                     set_reflection_effect(active_effect);
//                 } else {
//                     UtilityFunctions::print("[PlanarReflectorCPP2] setup_compositor_reflection_effect: First effect is not ReflectEffectPrePassGD");
//                 }
//             }
//         }
//     }
// }

// void PlanarReflectorCPP2::create_new_compositor_effect(Camera3D *reflect_cam) {
//     if (!reflect_cam) {
//         UtilityFunctions::print("[PlanarReflectorCPP2] create_new_compositor_effect: Invalid reflect camera");
//         return;
//     }

//     Ref<Compositor> current_compositor = reflect_cam->get_compositor();
//     if (current_compositor.is_valid()) {
//         clear_compositor_reflection_effect(reflect_cam);
//     }

//     Ref<Compositor> compositor = memnew(Compositor);

//     // Load GDScript ReflectEffectPrePassGD
//     Ref<Script> script = ResourceLoader::get_singleton()->load("res://addons/PlanarReflectorCpp/SupportFiles/GDScriptVersion/ReflectEffectGD.gd");
//     if (!script.is_valid()) {
//         UtilityFunctions::print("[PlanarReflectorCPP2] create_new_compositor_effect: Failed to load ReflectEffectGD.gd");
//         return;
//     }

//     Ref<CompositorEffect> prepass_effect = memnew(CompositorEffect);
//     prepass_effect->set_script(script);

//     UtilityFunctions::print("[PlanarReflectorCPP2] prepass_effect GDScript class name: ", script->get_class());


//     if (prepass_effect.is_valid() && prepass_effect->is_class("ReflectEffectPrePassGD")) {
//         double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
//         prepass_effect->set("intersect_height", height);
//         prepass_effect->set("effect_enabled", true);
//         prepass_effect->set("fill_enabled", fill_reflection_experimental);

//         TypedArray<CompositorEffect> effects;
//         effects.push_back(prepass_effect.ptr());

//         compositor->set_compositor_effects(effects);
//         reflect_cam->set_compositor(compositor);
//     } else {
//         UtilityFunctions::print("[PlanarReflectorCPP2] create_new_compositor_effect: Failed to create ReflectEffectPrePassGD");
//     }
// }

// ReflectionEffectPrePass* PlanarReflectorCPP2::set_reflection_effect(CompositorEffect *comp_effect) {
//     if (!comp_effect) {
//         UtilityFunctions::print("[PlanarReflectorCPP2] set_reflection_effect: Invalid compositor effect");
//         return nullptr;
//     }

//     if (comp_effect->is_class("ReflectEffectPrePassGD")) {
//         UtilityFunctions::print("[PlanarReflectorCPP2] set_reflection_effect: Effect is ReflectEffectPrePassGD");
//         double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
//         comp_effect->set("intersect_height", height);
//         comp_effect->set("effect_enabled", true);
//         comp_effect->set("fill_enabled", fill_reflection_experimental);
//         return static_cast<ReflectionEffectPrePass*>(comp_effect); // Cast to match signature
//     } else {
//         UtilityFunctions::print("[PlanarReflectorCPP2] set_reflection_effect: Effect is NOT ReflectEffectPrePassGD");
//     }
//     return nullptr;
// }

// void PlanarReflectorCPP2::clear_compositor_reflection_effect(Camera3D *reflect_cam) {
//     if (!reflect_cam) {
//         UtilityFunctions::print("[PlanarReflectorCPP2] clear_compositor_reflection_effect: Invalid reflect camera");
//         return;
//     }

//     Ref<Compositor> compositor = reflect_cam->get_compositor();
//     if (compositor.is_valid()) {
//         TypedArray<CompositorEffect> active_effects = compositor->get_compositor_effects();
//         if (active_effects.size() > 0) {
//             active_effects.clear();
//             reflect_cam->set_compositor(Ref<Compositor>());
//         }
//     }
// }

// CompositorEffect* PlanarReflectorCPP2::get_reflection_effect(Compositor *comp) {
//     if (!comp) {
//         UtilityFunctions::print("[PlanarReflectorCPP2] get_reflection_effect: Invalid compositor");
//         return nullptr;
//     }

//     TypedArray<CompositorEffect> effects = comp->get_compositor_effects();
//     for (int i = 0; i < effects.size(); i++) {
//         CompositorEffect *effect = Object::cast_to<CompositorEffect>(effects[i]);
//         if (effect && effect->is_class("ReflectEffectPrePassGD")) {
//             return effect;
//         }
//     }
//     UtilityFunctions::print("[PlanarReflectorCPP2] get_reflection_effect: No ReflectEffectPrePassGD found");
//     return nullptr;
// }

// void PlanarReflectorCPP2::inspect_effect_parameters(CompositorEffect *effect) {
//     if (!effect || !effect->is_class("ReflectEffectPrePassGD")) {
//         UtilityFunctions::print("[PlanarReflectorCPP2] inspect_effect_parameters: Invalid effect or not ReflectEffectPrePassGD");
//         return;
//     }

//     Array property_list = effect->get_property_list();
//     for (int i = 0; i < property_list.size(); i++) {
//         Dictionary prop = property_list[i];
//         String name = prop["name"];
//         Variant value = effect->get(name);
//         UtilityFunctions::print("[PlanarReflectorCPP2] Property: ", name, ", Value: ", value);
//     }
// }

// // Compositor Methods
// void PlanarReflectorCPP2::setup_compositor_reflection_effect(Camera3D *reflect_cam)
// {
//     if (!reflect_cam) {
//         return;
//     }

//     if (use_custom_compositor && custom_compositor) {
//         reflect_cam->set_compositor(Ref<Compositor>(custom_compositor));
//         Ref<Compositor> compositor = reflect_cam->get_compositor();
//         if (compositor.is_valid()) {
//             TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
//             if (effects.size() > 0) {
//                 CompositorEffect *active_effect = Object::cast_to<CompositorEffect>(effects[0]);
//                 if (Object::cast_to<ReflectionEffectPrePass>(active_effect)) {
//                     set_reflection_effect(active_effect);
//                 }
//             }
//         }
//     } else {
//         Ref<Compositor> current_compositor = reflect_cam->get_compositor();
//         if (!current_compositor.is_valid()) {
//             create_new_compositor_effect(reflect_cam);
//         } else {
//             TypedArray<CompositorEffect> effects = current_compositor->get_compositor_effects();
//             if (effects.size() > 0) {
//                 CompositorEffect *active_effect = Object::cast_to<CompositorEffect>(effects[0]);
//                 if (Object::cast_to<ReflectionEffectPrePass>(active_effect)) {
//                     set_reflection_effect(active_effect);
//                 }
//             }
//         }
//     }
// }

// void PlanarReflectorCPP2::create_new_compositor_effect(Camera3D *reflect_cam)
// {
//     if (!reflect_cam) {
//         return;
//     }
    
//     Ref<Compositor> current_compositor = reflect_cam->get_compositor();
//     if (current_compositor.is_valid()) {
//         clear_compositor_reflection_effect(reflect_cam);
//     }
    
//     Ref<Compositor> compositor;
//     compositor.instantiate();
    
//     Ref<ReflectionEffectPrePass> prepass_effect;
//     prepass_effect.instantiate();
    
//     if (prepass_effect.is_valid()) {
//         double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
//         prepass_effect->set_intersect_height(height);
//         prepass_effect->set_effect_enabled(true);
//         prepass_effect->set_fill_enabled(fill_reflection_experimental);
        
//         TypedArray<ReflectionEffectPrePass> effects;
//         effects.push_back(prepass_effect);
        
//         compositor->set_compositor_effects(effects);
//         reflect_cam->set_compositor(compositor);
//     }
// }

// ReflectionEffectPrePass* PlanarReflectorCPP2::set_reflection_effect(CompositorEffect *comp_effect)
// {
//     ReflectionEffectPrePass *reflection_effect = Object::cast_to<ReflectionEffectPrePass>(comp_effect);
//     if (reflection_effect) 
//     {
//         double height = override_YAxis_height ? new_YAxis_height : get_global_transform().get_origin().y;
//         reflection_effect->set_intersect_height(height);
//         reflection_effect->set_effect_enabled(true);
//         reflection_effect->set_fill_enabled(fill_reflection_experimental);
//         return reflection_effect;
//     }
//     return nullptr;
// }

// void PlanarReflectorCPP2::clear_compositor_reflection_effect(Camera3D *reflect_cam)
// {
//     if (!reflect_cam) {
//         return;
//     }
    
//     Ref<Compositor> compositor = reflect_cam->get_compositor();
//     if (compositor.is_valid()) {
//         TypedArray<CompositorEffect> active_effects = compositor->get_compositor_effects();
//         if (active_effects.size() > 0) {
//             active_effects.clear();
//             reflect_cam->set_compositor(Ref<Compositor>());
//         }
//     }
// }

// CompositorEffect* PlanarReflectorCPP2::get_reflection_effect(Compositor *comp)
// {
//     if (!comp) {
//         return nullptr;
//     }
    
//     TypedArray<CompositorEffect> effects = comp->get_compositor_effects();
//     for (int i = 0; i < effects.size(); i++) {
//         CompositorEffect *effect = Object::cast_to<CompositorEffect>(effects[i]);
//         if (Object::cast_to<ReflectionEffectPrePass>(effect)) {
//             return effect;
//         }
//     }
//     return nullptr;
// }

// Reflection Calculation Methods
Plane PlanarReflectorCPP2::calculate_reflection_plane()
{
    if (!is_inside_tree()) {
        return Plane();
    }
        
    Transform3D current_transform = get_global_transform();
    if (reflection_plane_cache_valid && current_transform.is_equal_approx(last_global_transform)) {
        return cached_reflection_plane;
    }
    
    Transform3D reflection_transform = current_transform * Transform3D().rotated(Vector3(1, 0, 0), Math_PI / 2.0);
    Vector3 plane_origin = reflection_transform.get_origin();
    Vector3 plane_normal = reflection_transform.get_basis().get_column(2).normalized();
    
    cached_reflection_plane = Plane(plane_normal, plane_origin.dot(plane_normal));
    last_global_transform = current_transform;
    reflection_plane_cache_valid = true;
    
    return cached_reflection_plane;
}

void PlanarReflectorCPP2::set_reflection_camera_transform()
{
    if (!is_inside_tree()) {
        return;
    }
        
    Camera3D *active_camera = get_active_camera();
    if (!active_camera || !reflect_camera) {
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
    update_shader_parameters();
}

void PlanarReflectorCPP2::update_camera_projection()
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

void PlanarReflectorCPP2::update_reflect_viewport_size()
{
    if (!reflect_viewport) {
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
    
    // Only update if size actually changed
    if (cached_viewport_size != target_size) {
        reflect_viewport->set_size(target_size);
        cached_viewport_size = target_size;
    }
}

void PlanarReflectorCPP2::update_shader_parameters()
{
    if (!is_material_cache_valid()) {
        refresh_material_cache();
    }
    
    ShaderMaterial *material = get_cached_material();
    if (!material || !reflect_viewport) {
        return;
    }
    
    Ref<Texture2D> reflection_texture = reflect_viewport->get_texture();
    bool is_orthogonal = false;
    
    Camera3D *active_cam = get_active_camera();
    if (active_cam) {
        is_orthogonal = (active_cam->get_projection() == Camera3D::PROJECTION_ORTHOGONAL);
    }
    
    // Prepare all parameters for batch comparison
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

Transform3D PlanarReflectorCPP2::apply_reflection_offset(const Transform3D &base_transform)
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

void PlanarReflectorCPP2::update_offset_cache()
{
    if (!enable_reflection_offset) {
        cached_offset_transform = Transform3D();
        return;
    }
    
    // Only recalculate if values actually changed
    if (last_offset_position.is_equal_approx(reflection_offset_position) && 
        last_offset_rotation.is_equal_approx(reflection_offset_rotation)) {
        return;
    }
    
    Basis offset_basis;
    offset_basis = offset_basis.rotated(Vector3(1, 0, 0), Math::deg_to_rad(reflection_offset_rotation.x));
    offset_basis = offset_basis.rotated(Vector3(0, 1, 0), Math::deg_to_rad(reflection_offset_rotation.y));
    offset_basis = offset_basis.rotated(Vector3(0, 0, 1), Math::deg_to_rad(reflection_offset_rotation.z));
    
    cached_offset_transform = Transform3D(offset_basis, reflection_offset_position * reflection_offset_scale);
    
    last_offset_position = reflection_offset_position;
    last_offset_rotation = reflection_offset_rotation;
}

bool PlanarReflectorCPP2::should_update_reflection(Camera3D *active_cam)
{
    if (!active_cam || !is_inside_tree()) {
        return false;
    }

    Vector3 current_pos = active_cam->get_global_transform().get_origin();
    Basis current_basis = active_cam->get_global_transform().get_basis();
    
    // Check if camera moved/rotated enough to warrant update
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

// Performance Helper Methods
bool PlanarReflectorCPP2::is_material_cache_valid()
{
    if (!material_cache_valid || !cached_material_pointer) {
        return false;
    }
    
    // Check if the material pointer is still valid by comparing with current material
    if (get_surface_override_material_count() == 0) {
        return false;
    }
    
    Ref<Material> current_material = get_active_material(0);
    return current_material.is_valid() && current_material.ptr() == cached_material_pointer;
}

void PlanarReflectorCPP2::refresh_material_cache()
{
    if (!get_mesh().is_valid() || get_surface_override_material_count() == 0) {
        cached_material_pointer = nullptr;
        material_cache_valid = false;
        return;
    }

    Ref<Material> material = get_active_material(0);
    if (material.is_valid() && Object::cast_to<ShaderMaterial>(material.ptr())) {
        cached_material_pointer = Object::cast_to<ShaderMaterial>(material.ptr());
        material_cache_valid = true;
    } else {
        cached_material_pointer = nullptr;
        material_cache_valid = false;
    }
}

ShaderMaterial* PlanarReflectorCPP2::get_cached_material()
{
    if (is_material_cache_valid()) {
        return cached_material_pointer;
    }
    return nullptr;
}

bool PlanarReflectorCPP2::values_equal(Variant a, Variant b)
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

Vector2i PlanarReflectorCPP2::get_target_viewport_size()
{
    Vector2i target_size;
    if (Engine::get_singleton()->is_editor_hint() && editor_helper && editor_helper->has_method("get_editor_viewport_size")) {
        Variant result = editor_helper->call("get_editor_viewport_size");
        if (result.get_type() == Variant::VECTOR2I) {
            target_size = result;
        } else {
            target_size = get_viewport()->get_visible_rect().size;
        }
    } else {
        target_size = get_viewport()->get_visible_rect().size;
    }
    return target_size;
}

Vector2i PlanarReflectorCPP2::apply_lod_to_size(Vector2i target_size, Camera3D *active_cam)
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

void PlanarReflectorCPP2::invalidate_all_caches()
{
    material_cache_valid = false;
    cached_material_pointer = nullptr;
    cached_shader_params.clear();
    reflection_plane_cache_valid = false;
    last_viewport_check_frame = -1;
    cached_viewport_size = Vector2i(0, 0);
}

// Public Interface Methods - CRITICAL FOR PLUGIN HELPER
void PlanarReflectorCPP2::set_editor_camera(Camera3D *viewport_camera)
{
    editor_camera = viewport_camera;
    invalidate_all_caches();
    update_reflect_viewport_size();
    set_reflection_camera_transform();
}

Camera3D* PlanarReflectorCPP2::get_active_camera()
{
    if (Engine::get_singleton()->is_editor_hint()) {
        return editor_camera ? editor_camera : main_camera;
    }
    return main_camera;
}

bool PlanarReflectorCPP2::is_planar_reflector_active()
{
    return is_active;
}

void PlanarReflectorCPP2::_exit_tree()
{
    // Clean shutdown - avoid aggressive cleanup
    // if (reflect_camera && reflect_camera->get_compositor().is_valid()) {
    //     clear_compositor_reflection_effect(reflect_camera);
    // }
    
    invalidate_all_caches();
    editor_helper = nullptr;
    active_compositor = nullptr;
    custom_environment = nullptr;
}

// Method Bindings
void PlanarReflectorCPP2::_bind_methods() 
{
    // Core property
    ClassDB::bind_method(D_METHOD("set_is_active", "p_active"), &PlanarReflectorCPP2::set_is_active);
    ClassDB::bind_method(D_METHOD("get_is_active"), &PlanarReflectorCPP2::get_is_active);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_active"), "set_is_active", "get_is_active");

    // Core camera and resolution properties
    ClassDB::bind_method(D_METHOD("set_main_camera", "p_camera"), &PlanarReflectorCPP2::set_main_camera);
    ClassDB::bind_method(D_METHOD("get_main_camera"), &PlanarReflectorCPP2::get_main_camera);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "main_camera", PROPERTY_HINT_NODE_TYPE, "Camera3D"), "set_main_camera", "get_main_camera");
    
    ClassDB::bind_method(D_METHOD("set_reflection_camera_resolution", "p_resolution"), &PlanarReflectorCPP2::set_reflection_camera_resolution);
    ClassDB::bind_method(D_METHOD("get_reflection_camera_resolution"), &PlanarReflectorCPP2::get_reflection_camera_resolution);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "reflection_camera_resolution"), "set_reflection_camera_resolution", "get_reflection_camera_resolution");

    // Camera Controls Group
    ADD_GROUP("Camera Controls", "");
    ClassDB::bind_method(D_METHOD("set_ortho_scale_multiplier", "p_multiplier"), &PlanarReflectorCPP2::set_ortho_scale_multiplier);
    ClassDB::bind_method(D_METHOD("get_ortho_scale_multiplier"), &PlanarReflectorCPP2::get_ortho_scale_multiplier);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ortho_scale_multiplier"), "set_ortho_scale_multiplier", "get_ortho_scale_multiplier");

    ClassDB::bind_method(D_METHOD("set_ortho_uv_scale", "p_scale"), &PlanarReflectorCPP2::set_ortho_uv_scale);
    ClassDB::bind_method(D_METHOD("get_ortho_uv_scale"), &PlanarReflectorCPP2::get_ortho_uv_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ortho_uv_scale"), "set_ortho_uv_scale", "get_ortho_uv_scale");
    
    ClassDB::bind_method(D_METHOD("set_auto_detect_camera_mode", "p_auto_detect"), &PlanarReflectorCPP2::set_auto_detect_camera_mode);
    ClassDB::bind_method(D_METHOD("get_auto_detect_camera_mode"), &PlanarReflectorCPP2::get_auto_detect_camera_mode);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_detect_camera_mode"), "set_auto_detect_camera_mode", "get_auto_detect_camera_mode");

    // Reflection Layers and Environment Group
    ADD_GROUP("Reflection Layers and Environment", "");
    ClassDB::bind_method(D_METHOD("set_reflection_layers", "p_layers"), &PlanarReflectorCPP2::set_reflection_layers);
    ClassDB::bind_method(D_METHOD("get_reflection_layers"), &PlanarReflectorCPP2::get_reflection_layers);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_reflection_layers", "get_reflection_layers");

    ClassDB::bind_method(D_METHOD("set_use_custom_environment", "p_use_custom"), &PlanarReflectorCPP2::set_use_custom_environment);
    ClassDB::bind_method(D_METHOD("get_use_custom_environment"), &PlanarReflectorCPP2::get_use_custom_environment);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_custom_environment"), "set_use_custom_environment", "get_use_custom_environment");

    ClassDB::bind_method(D_METHOD("set_custom_environment", "p_environment"), &PlanarReflectorCPP2::set_custom_environment);
    ClassDB::bind_method(D_METHOD("get_custom_environment"), &PlanarReflectorCPP2::get_custom_environment);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "custom_environment", PROPERTY_HINT_RESOURCE_TYPE, "Environment"), "set_custom_environment", "get_custom_environment");

    // Reflection Compositor Effects Group
    ADD_GROUP("Reflection Compositor Effects", "");
    // ClassDB::bind_method(D_METHOD("set_use_custom_compositor", "p_use_custom"), &PlanarReflectorCPP2::set_use_custom_compositor);
    // ClassDB::bind_method(D_METHOD("get_use_custom_compositor"), &PlanarReflectorCPP2::get_use_custom_compositor);
    // ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_custom_compositor"), "set_use_custom_compositor", "get_use_custom_compositor");

    ClassDB::bind_method(D_METHOD("set_active_compositor", "p_compositor"), &PlanarReflectorCPP2::set_active_compositor);
    ClassDB::bind_method(D_METHOD("get_active_compositor"), &PlanarReflectorCPP2::get_active_compositor);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "active_compositor", PROPERTY_HINT_RESOURCE_TYPE, "Compositor"), "set_active_compositor", "get_active_compositor");

    ClassDB::bind_method(D_METHOD("set_hide_intersect_reflections", "p_hide"), &PlanarReflectorCPP2::set_hide_intersect_reflections);
    ClassDB::bind_method(D_METHOD("get_hide_intersect_reflections"), &PlanarReflectorCPP2::get_hide_intersect_reflections);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hide_intersect_reflections"), "set_hide_intersect_reflections", "get_hide_intersect_reflections");

    ClassDB::bind_method(D_METHOD("set_override_YAxis_height", "p_override"), &PlanarReflectorCPP2::set_override_YAxis_height);
    ClassDB::bind_method(D_METHOD("get_override_YAxis_height"), &PlanarReflectorCPP2::get_override_YAxis_height);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "override_YAxis_height"), "set_override_YAxis_height", "get_override_YAxis_height");

    ClassDB::bind_method(D_METHOD("set_new_YAxis_height", "p_height"), &PlanarReflectorCPP2::set_new_YAxis_height);
    ClassDB::bind_method(D_METHOD("get_new_YAxis_height"), &PlanarReflectorCPP2::get_new_YAxis_height);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "new_YAxis_height"), "set_new_YAxis_height", "get_new_YAxis_height");

    ClassDB::bind_method(D_METHOD("set_fill_reflection_experimental", "p_fill"), &PlanarReflectorCPP2::set_fill_reflection_experimental);
    ClassDB::bind_method(D_METHOD("get_fill_reflection_experimental"), &PlanarReflectorCPP2::get_fill_reflection_experimental);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fill_reflection_experimental"), "set_fill_reflection_experimental", "get_fill_reflection_experimental");

    // Reflection Offset Control Group
    ADD_GROUP("Reflection Offset Control", "");
    ClassDB::bind_method(D_METHOD("set_enable_reflection_offset", "p_enable"), &PlanarReflectorCPP2::set_enable_reflection_offset);
    ClassDB::bind_method(D_METHOD("get_enable_reflection_offset"), &PlanarReflectorCPP2::get_enable_reflection_offset);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_reflection_offset"), "set_enable_reflection_offset", "get_enable_reflection_offset");

    ClassDB::bind_method(D_METHOD("set_reflection_offset_position", "p_position"), &PlanarReflectorCPP2::set_reflection_offset_position);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_position"), &PlanarReflectorCPP2::get_reflection_offset_position);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "reflection_offset_position"), "set_reflection_offset_position", "get_reflection_offset_position");

    ClassDB::bind_method(D_METHOD("set_reflection_offset_rotation", "p_rotation"), &PlanarReflectorCPP2::set_reflection_offset_rotation);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_rotation"), &PlanarReflectorCPP2::get_reflection_offset_rotation);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "reflection_offset_rotation"), "set_reflection_offset_rotation", "get_reflection_offset_rotation");

    ClassDB::bind_method(D_METHOD("set_reflection_offset_scale", "p_scale"), &PlanarReflectorCPP2::set_reflection_offset_scale);
    ClassDB::bind_method(D_METHOD("get_reflection_offset_scale"), &PlanarReflectorCPP2::get_reflection_offset_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_offset_scale"), "set_reflection_offset_scale", "get_reflection_offset_scale");

    ClassDB::bind_method(D_METHOD("set_offset_blend_mode", "p_mode"), &PlanarReflectorCPP2::set_offset_blend_mode);
    ClassDB::bind_method(D_METHOD("get_offset_blend_mode"), &PlanarReflectorCPP2::get_offset_blend_mode);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "offset_blend_mode", PROPERTY_HINT_ENUM, "Add,Multiply,Screen Space Shift"), "set_offset_blend_mode", "get_offset_blend_mode");

    // Performance Controls Group
    ADD_GROUP("Performance Controls", "");
    ClassDB::bind_method(D_METHOD("set_update_frequency", "p_frequency"), &PlanarReflectorCPP2::set_update_frequency);
    ClassDB::bind_method(D_METHOD("get_update_frequency"), &PlanarReflectorCPP2::get_update_frequency);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "update_frequency", PROPERTY_HINT_RANGE, "1,10,1"), "set_update_frequency", "get_update_frequency");

    ClassDB::bind_method(D_METHOD("set_use_lod", "p_use_lod"), &PlanarReflectorCPP2::set_use_lod);
    ClassDB::bind_method(D_METHOD("get_use_lod"), &PlanarReflectorCPP2::get_use_lod);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_lod"), "set_use_lod", "get_use_lod");

    ClassDB::bind_method(D_METHOD("set_lod_distance_near", "p_distance"), &PlanarReflectorCPP2::set_lod_distance_near);
    ClassDB::bind_method(D_METHOD("get_lod_distance_near"), &PlanarReflectorCPP2::get_lod_distance_near);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance_near"), "set_lod_distance_near", "get_lod_distance_near");

    ClassDB::bind_method(D_METHOD("set_lod_distance_far", "p_distance"), &PlanarReflectorCPP2::set_lod_distance_far);
    ClassDB::bind_method(D_METHOD("get_lod_distance_far"), &PlanarReflectorCPP2::get_lod_distance_far);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance_far"), "set_lod_distance_far", "get_lod_distance_far");

    ClassDB::bind_method(D_METHOD("set_lod_resolution_multiplier", "p_multiplier"), &PlanarReflectorCPP2::set_lod_resolution_multiplier);
    ClassDB::bind_method(D_METHOD("get_lod_resolution_multiplier"), &PlanarReflectorCPP2::get_lod_resolution_multiplier);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_resolution_multiplier", PROPERTY_HINT_RANGE, "0.1,1.0,0.01"), "set_lod_resolution_multiplier", "get_lod_resolution_multiplier");

    // Utility methods for editor integration - CRITICAL FOR PLUGIN HELPER
    ClassDB::bind_method(D_METHOD("set_editor_camera", "viewport_camera"), &PlanarReflectorCPP2::set_editor_camera);
    ClassDB::bind_method(D_METHOD("get_active_camera"), &PlanarReflectorCPP2::get_active_camera);
    ClassDB::bind_method(D_METHOD("is_planar_reflector_active"), &PlanarReflectorCPP2::is_planar_reflector_active);
    
    // CRITICAL: Bind the initial_setup method so it can be called deferred
    ClassDB::bind_method(D_METHOD("initial_setup"), &PlanarReflectorCPP2::initial_setup);
    
    // CRITICAL: Additional methods that plugin helper needs
    ClassDB::bind_method(D_METHOD("update_reflect_viewport_size"), &PlanarReflectorCPP2::update_reflect_viewport_size);
    ClassDB::bind_method(D_METHOD("set_reflection_camera_transform"), &PlanarReflectorCPP2::set_reflection_camera_transform);
    
    // Method bindings for manual calls (useful for debugging and plugin integration)
    ClassDB::bind_method(D_METHOD("setup_reflection_camera_and_viewport"), &PlanarReflectorCPP2::setup_reflection_camera_and_viewport);
    ClassDB::bind_method(D_METHOD("invalidate_all_caches"), &PlanarReflectorCPP2::invalidate_all_caches);
}

// SETTERS AND GETTERS IMPLEMENTATION

void PlanarReflectorCPP2::set_is_active(const bool p_active) { is_active = p_active; }
bool PlanarReflectorCPP2::get_is_active() const { return is_active; }

void PlanarReflectorCPP2::set_main_camera(Camera3D *p_camera) 
{
    main_camera = Object::cast_to<Camera3D>(p_camera);
    
    if (reflect_camera && main_camera) {
        reflect_camera->set_attributes(main_camera->get_attributes());
        reflect_camera->set_doppler_tracking(main_camera->get_doppler_tracking());
        setup_reflection_environment();
    }
}
Camera3D* PlanarReflectorCPP2::get_main_camera() const { return main_camera; }

void PlanarReflectorCPP2::set_reflection_camera_resolution(const Vector2i p_resolution) 
{ 
    reflection_camera_resolution = p_resolution;
    if (reflect_viewport) {
        reflect_viewport->set_size(reflection_camera_resolution);
        cached_viewport_size = reflection_camera_resolution;
    }
}
Vector2i PlanarReflectorCPP2::get_reflection_camera_resolution() const { return reflection_camera_resolution; }

// Camera Controls Group
void PlanarReflectorCPP2::set_ortho_scale_multiplier(double p_multiplier) { ortho_scale_multiplier = p_multiplier; }
double PlanarReflectorCPP2::get_ortho_scale_multiplier() const { return ortho_scale_multiplier; }

void PlanarReflectorCPP2::set_ortho_uv_scale(double p_scale) { ortho_uv_scale = p_scale; }
double PlanarReflectorCPP2::get_ortho_uv_scale() const { return ortho_uv_scale; }

void PlanarReflectorCPP2::set_auto_detect_camera_mode(bool p_auto_detect) { auto_detect_camera_mode = p_auto_detect; }
bool PlanarReflectorCPP2::get_auto_detect_camera_mode() const { return auto_detect_camera_mode; }

// Reflection Layers and Environment Group
void PlanarReflectorCPP2::set_reflection_layers(int p_layers)
{
    reflection_layers = p_layers;    
    if (reflect_camera) {
        int cull_mask = reflection_layers;
        reflect_camera->set_cull_mask(cull_mask);
        is_layer_one_active = bool(cull_mask & (1 << 0));
    }
}
int PlanarReflectorCPP2::get_reflection_layers() const { return reflection_layers; }

void PlanarReflectorCPP2::set_use_custom_environment(bool p_use_custom)
{
    use_custom_environment = p_use_custom;
    if (is_inside_tree()) {
        setup_reflection_environment();
    }
}
bool PlanarReflectorCPP2::get_use_custom_environment() const { return use_custom_environment; }

void PlanarReflectorCPP2::set_custom_environment(Environment *p_environment)
{
    custom_environment = Object::cast_to<Environment>(p_environment);
    if (use_custom_environment && is_inside_tree()) {
        setup_reflection_environment();
    }
}
Environment* PlanarReflectorCPP2::get_custom_environment() const { return custom_environment; }

// Reflection Compositor Effects Group
// void PlanarReflectorCPP2::set_use_custom_compositor(bool p_use_custom)
// {
//     use_custom_compositor = p_use_custom;
//     if (reflect_camera && is_inside_tree()) {
//         setup_compositor_reflection_effect(reflect_camera);
//     }
// }
// bool PlanarReflectorCPP2::get_use_custom_compositor() const { return use_custom_compositor; }

void PlanarReflectorCPP2::set_active_compositor(Compositor *p_compositor)
{
    active_compositor = Object::cast_to<Compositor>(p_compositor);
    // if (use_custom_compositor && reflect_camera && is_inside_tree()) 
    if (reflect_camera && is_inside_tree()) 
    {
        //TODO: //DEBUG -CHECK IF WE NEED A SETTER TO DO SOMETHING WHEN WE APPLY A NEW ONE MANUALLY
        //TODO: POTENTIALLY BBREAK DOWN THE setup_compositor_reflection_effect INTO A SEPARATE ONE JUST TO "UPDATE PARAMETERS"
        // AND THENW E CAN JUST THE "UPDATE PARAMETERS" WHEN WE SET A NEW COMPOSITOR
        // setup_compositor_reflection_effect(reflect_camera);
    }
}
Compositor* PlanarReflectorCPP2::get_active_compositor() const { return active_compositor; }

void PlanarReflectorCPP2::set_hide_intersect_reflections(bool p_hide)
{
    hide_intersect_reflections = p_hide;
    if (reflect_camera && is_inside_tree()) {
        setup_compositor_reflection_effect(reflect_camera);
    }
}
bool PlanarReflectorCPP2::get_hide_intersect_reflections() const { return hide_intersect_reflections; }

void PlanarReflectorCPP2::set_override_YAxis_height(bool p_override)
{
    override_YAxis_height = p_override;
    if (reflect_camera && is_inside_tree()) {
        setup_compositor_reflection_effect(reflect_camera);
    }
}
bool PlanarReflectorCPP2::get_override_YAxis_height() const { return override_YAxis_height; }

void PlanarReflectorCPP2::set_new_YAxis_height(double p_height)
{
    new_YAxis_height = p_height;
    if (reflect_camera && is_inside_tree()) {
        setup_compositor_reflection_effect(reflect_camera);
    }
}
double PlanarReflectorCPP2::get_new_YAxis_height() const { return new_YAxis_height; }

void PlanarReflectorCPP2::set_fill_reflection_experimental(bool p_fill)
{
    fill_reflection_experimental = p_fill;
    if (reflect_camera && is_inside_tree()) {
        setup_compositor_reflection_effect(reflect_camera);
    }
}
bool PlanarReflectorCPP2::get_fill_reflection_experimental() const { return fill_reflection_experimental; }

// Reflection Offset Control Group
void PlanarReflectorCPP2::set_enable_reflection_offset(bool p_enable)
{
    enable_reflection_offset = p_enable;
    update_offset_cache();
}
bool PlanarReflectorCPP2::get_enable_reflection_offset() const { return enable_reflection_offset; }

void PlanarReflectorCPP2::set_reflection_offset_position(const Vector3 &p_position)
{
    reflection_offset_position = p_position;
    update_offset_cache();
}
Vector3 PlanarReflectorCPP2::get_reflection_offset_position() const { return reflection_offset_position; }

void PlanarReflectorCPP2::set_reflection_offset_rotation(const Vector3 &p_rotation)
{
    reflection_offset_rotation = p_rotation;
    update_offset_cache();
}
Vector3 PlanarReflectorCPP2::get_reflection_offset_rotation() const { return reflection_offset_rotation; }

void PlanarReflectorCPP2::set_reflection_offset_scale(double p_scale)
{
    reflection_offset_scale = p_scale;
    update_offset_cache();
}
double PlanarReflectorCPP2::get_reflection_offset_scale() const { return reflection_offset_scale; }

void PlanarReflectorCPP2::set_offset_blend_mode(int p_mode) { offset_blend_mode = Math::clamp(p_mode, 0, 2); }
int PlanarReflectorCPP2::get_offset_blend_mode() const { return offset_blend_mode; }

// Performance Controls Group
void PlanarReflectorCPP2::set_update_frequency(int p_frequency) { update_frequency = Math::max(p_frequency, 1); }
int PlanarReflectorCPP2::get_update_frequency() const { return update_frequency; }

void PlanarReflectorCPP2::set_use_lod(bool p_use_lod) { use_lod = p_use_lod; }
bool PlanarReflectorCPP2::get_use_lod() const { return use_lod; }

void PlanarReflectorCPP2::set_lod_distance_near(double p_distance) { lod_distance_near = p_distance; }
double PlanarReflectorCPP2::get_lod_distance_near() const { return lod_distance_near; }

void PlanarReflectorCPP2::set_lod_distance_far(double p_distance) { lod_distance_far = p_distance; }
double PlanarReflectorCPP2::get_lod_distance_far() const { return lod_distance_far; }

void PlanarReflectorCPP2::set_lod_resolution_multiplier(double p_multiplier) { lod_resolution_multiplier = p_multiplier; }
double PlanarReflectorCPP2::get_lod_resolution_multiplier() const { return lod_resolution_multiplier; }