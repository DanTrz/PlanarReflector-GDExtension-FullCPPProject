#ifndef PLANAR_REFLECTOR_CPP_H
#define PLANAR_REFLECTOR_CPP_H

//MUST INCLUDE GODOT CLASSES YOU NEED ACCESS TO VIA HERE
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/camera_attributes.hpp>
#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/dictionary.hpp>
// Forward declaration for our C++ ReflectionEffectPrePass
namespace godot {
    class ReflectionEffectPrePass;
}

namespace godot {

    class PlanarReflectorCPP : public MeshInstance3D 
    {
        GDCLASS(PlanarReflectorCPP, MeshInstance3D)
    
    private:
        // SIMPLIFIED SINGLE VIEWPORT APPROACH (like GDScript)
        Camera3D *main_camera = nullptr;
        Camera3D *editor_camera = nullptr;
        Camera3D *reflect_camera = nullptr;
        SubViewport *reflect_viewport = nullptr;
        
        // Editor helper - simplified approach
        Object* editor_helper = nullptr;
        Object* reflect_effect_gdscript = nullptr;

        
        // Resolution and camera controls
        Vector2i reflection_camera_resolution = Vector2i(1920, 1080);
        double ortho_scale_multiplier = 1.0;
        double ortho_uv_scale = 1.0;
        bool auto_detect_camera_mode = true;

        // Layer and environment control
        int reflection_layers = 1;
        bool use_custom_environment = false;
        Environment *custom_environment = nullptr;

        // Reflection Compositor Effects
        // bool use_custom_compositor = false;
        Ref<Compositor> active_compositor;
        bool hide_intersect_reflections = true;
        bool override_YAxis_height = false;
        double new_YAxis_height = 0.0;
        bool fill_reflection_experimental = false;

        // Reflection Offset Controls
        bool enable_reflection_offset = false;
        Vector3 reflection_offset_position = Vector3(0.0, 0.0, 0.0);
        Vector3 reflection_offset_rotation = Vector3(0.0, 0.0, 0.0);
        double reflection_offset_scale = 1.0;
        int offset_blend_mode = 0;

        // Performance parameters
        int update_frequency = 2;
        bool use_lod = false;
        double lod_distance_near = 10.0;
        double lod_distance_far = 25.0;
        double lod_resolution_multiplier = 0.45;

        // Internal optimization variables
        int frame_counter = 0;
        Vector3 last_camera_position = Vector3();
        Basis last_camera_rotation = Basis();
        double position_threshold = 0.01;
        double rotation_threshold = 0.001;

        // Cached calculations
        Plane cached_reflection_plane = Plane();
        bool is_layer_one_active = true;
        Transform3D cached_offset_transform = Transform3D();
        Vector3 last_offset_position = Vector3();
        Vector3 last_offset_rotation = Vector3();

        // Performance caches (from GDScript improvements)
        // ShaderMaterial* cached_material_pointer = nullptr;
        // bool material_cache_valid = false;
        // Dictionary cached_shader_params;
        // Vector2i cached_viewport_size = Vector2i(0, 0);
        int last_viewport_check_frame = -1;
        int viewport_check_frequency = 5;
        Transform3D last_global_transform = Transform3D();
        bool reflection_plane_cache_valid = false;
        double last_distance_check = -1.0;
        double cached_lod_factor = 1.0;

        // Core setup methods - SIMPLIFIED
        void initial_setup();
        void setup_reflection_camera_and_viewport();
        void setup_reflection_environment();
        void find_editor_helper();

        
        // Compositor methods
        void setup_compositor_reflection_effect(Camera3D *reflect_cam);
        void update_compositor_parameters();
        Ref<Compositor> create_new_compositor();bool compositor_was_set_explicitly = false; 
        ReflectionEffectPrePass* set_reflection_effect(CompositorEffect *comp_effect);
        void clear_compositor_reflection_effect(Camera3D *reflect_cam);
        CompositorEffect* get_reflection_effect(Compositor *comp);
        
        // Reflection calculation methods
        Plane calculate_reflection_plane();
        void set_reflection_camera_transform();
        void update_camera_projection();
        void update_reflect_viewport_size();
        void update_shader_parameters();
        Transform3D apply_reflection_offset(const Transform3D &base_transform);
        void update_offset_cache();
        bool should_update_reflection(Camera3D *active_cam);
        
        // Performance helper methods
        // bool is_material_cache_valid();
        // void refresh_material_cache();
        // ShaderMaterial* get_cached_material();
        // bool values_equal(Variant a, Variant b);
        Vector2i get_target_viewport_size();
        Vector2i apply_lod_to_size(Vector2i target_size, Camera3D *active_cam);
        void invalidate_all_caches();

        void create_viewport_deferred();
        void clear_shader_texture_references();
        void complete_cleanup();
        void finalize_setup();

    protected:
        static void _bind_methods();

    public:
        PlanarReflectorCPP();
        ~PlanarReflectorCPP();

        void _process(double delta) override; 
        void _ready() override;
        void _exit_tree() override;
        void _notification(int what);
        
        bool is_active = true;

        // Public utility methods for editor integration - CRITICAL FOR PLUGIN HELPER
        void set_editor_camera(Camera3D *viewport_camera);
        Camera3D* get_active_camera(); // Returns active camera for plugin helper
        bool is_planar_reflector_active();

        // Setters and Getters
        void set_is_active(bool p_active);
        bool get_is_active() const;
        
        // Core camera and resolution controls
        void set_main_camera(Camera3D *p_camera);
        Camera3D *get_main_camera() const;

        void set_reflection_camera_resolution(const Vector2i p_resolution);
        Vector2i get_reflection_camera_resolution() const;

        // Camera Controls Group
        void set_ortho_scale_multiplier(double p_multiplier);
        double get_ortho_scale_multiplier() const;

        void set_ortho_uv_scale(double p_scale);
        double get_ortho_uv_scale() const;

        void set_auto_detect_camera_mode(bool p_auto_detect);
        bool get_auto_detect_camera_mode() const;

        // Reflection Layers and Environment Group
        void set_reflection_layers(int p_layers);
        int get_reflection_layers() const;

        void set_use_custom_environment(bool p_use_custom);
        bool get_use_custom_environment() const;

        void set_custom_environment(Environment *p_environment);
        Environment *get_custom_environment() const;

        // Reflection Compositor Effects Group
        // void set_use_custom_compositor(bool p_use_custom);
        // bool get_use_custom_compositor() const;

        void set_active_compositor(Compositor *p_compositor);
        Compositor *get_active_compositor() const;

        void set_hide_intersect_reflections(bool p_hide);
        bool get_hide_intersect_reflections() const;

        void set_override_YAxis_height(bool p_override);
        bool get_override_YAxis_height() const;

        void set_new_YAxis_height(double p_height);
        double get_new_YAxis_height() const;

        void set_fill_reflection_experimental(bool p_fill);
        bool get_fill_reflection_experimental() const;

        // Reflection Offset Control Group
        void set_enable_reflection_offset(bool p_enable);
        bool get_enable_reflection_offset() const;

        void set_reflection_offset_position(const Vector3 &p_position);
        Vector3 get_reflection_offset_position() const;

        void set_reflection_offset_rotation(const Vector3 &p_rotation);
        Vector3 get_reflection_offset_rotation() const;

        void set_reflection_offset_scale(double p_scale);
        double get_reflection_offset_scale() const;

        void set_offset_blend_mode(int p_mode);
        int get_offset_blend_mode() const;

        // Performance Controls Group
        void set_update_frequency(int p_frequency);
        int get_update_frequency() const;

        void set_use_lod(bool p_use_lod);
        bool get_use_lod() const;

        void set_lod_distance_near(double p_distance);
        double get_lod_distance_near() const;

        void set_lod_distance_far(double p_distance);
        double get_lod_distance_far() const;

        void set_lod_resolution_multiplier(double p_multiplier);
        double get_lod_resolution_multiplier() const;
    };

}
#endif