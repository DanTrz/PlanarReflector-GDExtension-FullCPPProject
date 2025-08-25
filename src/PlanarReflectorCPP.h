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
    
    //Define Variables/functions => group variables by permission (Private, protected, etc)
    private:
        // Core GAME reflection components
        Camera3D *game_main_camera = nullptr;
        Camera3D *game_reflect_camera = nullptr;
        SubViewport *game_reflect_viewport = nullptr;

        // Core Editor reflection components
        Camera3D *editor_camera = nullptr;
        Camera3D *editor_reflect_camera = nullptr;
        SubViewport *editor_reflect_viewport = nullptr;
        Object* editor_helper = nullptr;
        bool is_editor_setup_finished = false;

        // Core Active reflection components (pointers to either game or editor components)
        Camera3D *active_main_camera = nullptr;
        Camera3D *active_reflect_camera = nullptr;
        SubViewport *active_reflect_viewport = nullptr;
        
        // Resolution and camera controls
        Vector2i reflection_camera_resolution = Vector2i(1920, 1080);
        double ortho_scale_multiplier = 1.0;
        double ortho_uv_scale = 1.0;
        bool auto_detect_camera_mode = true;

        // Layer and environment control
        int reflection_layers = 1; // Using int for flags_3d_render
        bool use_custom_environment = true;
        Environment *custom_environment = nullptr;

        // Reflection Compositor Effects - NEW C++ INTEGRATION
        bool use_custom_compositor = false;
        Compositor *custom_compositor = nullptr;
        bool hide_intersect_reflections = true;
        bool override_YAxis_height = false;
        double new_YAxis_height = 0.0;
        bool fill_reflection_experimental = false;

        // Reflection Offset Controls
        bool enable_reflection_offset = false;
        Vector3 reflection_offset_position = Vector3(0.0, 0.0, 0.0);
        Vector3 reflection_offset_rotation = Vector3(0.0, 0.0, 0.0);
        double reflection_offset_scale = 1.0;
        int offset_blend_mode = 0; // 0 = Add, 1 = Multiply, 2 = Screen space shift

        // Performance parameters
        int update_frequency = 1; // Update every N frames
        bool use_lod = true; // Auto reduce reflection resolution based on distance
        double lod_distance_near = 10.0;
        double lod_distance_far = 30.0;
        double lod_resolution_multiplier = 0.45;

        // Internal optimization variables - ENHANCED FROM GDSCRIPT
        ShaderMaterial *active_shader_material = nullptr;
        int frame_counter = 0;
        Vector3 last_camera_position = Vector3();
        Basis last_camera_rotation = Basis();
        double position_threshold = 0.01; // Only update if camera moved this much
        double rotation_threshold = 0.001; // Only update if camera rotated this much

        // Advanced reflection calculation cache
        Plane cached_reflection_plane = Plane();
        bool is_layer_one_active = true;

        // Offset calculation cache
        Transform3D cached_offset_transform = Transform3D();
        Vector3 last_offset_position = Vector3();
        Vector3 last_offset_rotation = Vector3();

        // PERFORMANCE IMPROVEMENTS FROM GDSCRIPT
        // Enhanced material caching with proper invalidation
        bool material_cache_valid = false;
        
        // Batch shader parameter updates with change detection
        Dictionary cached_shader_params;
        Ref<Texture2D> last_reflection_texture;
        
        // Optimized viewport size checking
        Vector2i cached_viewport_size = Vector2i(0, 0);
        int last_viewport_check_frame = -1;
        int viewport_check_frequency = 5;
        
        // Enhanced compositor effect lifecycle management
        bool compositor_effect_initialized = false;
        Dictionary last_compositor_settings;
        
        // Cached reflection plane calculations
        Transform3D last_global_transform = Transform3D();
        bool reflection_plane_cache_valid = false;
        double last_distance_check = -1.0;
        double cached_lod_factor = 1.0;

        // Internal helper methods
        void clean_viewports();
        void run_game_setup_init();
        Camera3D* get_active_camera();
        void run_editor_setup_init();
        void find_editor_helper();
        void setup_reflection_viewport();
        void setup_reflection_camera();
        void setup_reflection_layers();
        void setup_reflection_environment();
        
        // ENHANCED COMPOSITOR METHODS - NEW C++ INTEGRATION
        void setup_compositor_reflection_effect(Camera3D *reflect_cam);
        void create_new_compositor_effect(Camera3D *reflect_cam);
        ReflectionEffectPrePass* set_reflection_effect(CompositorEffect *comp_effect);
        void clear_compositor_reflection_effect(Camera3D *reflect_cam);
        CompositorEffect* get_reflection_effect(Compositor *comp);
        bool compositor_settings_equal(const Dictionary &a, const Dictionary &b);
        
        Plane calculate_reflection_plane();
        void update_reflection_camera();
        void update_camera_projection();
        void update_viewport();
        void update_shader_parameters();
        Transform3D apply_reflection_offset(const Transform3D &base_transform);
        void update_offset_cache();
        bool should_update_reflection();
        
        // PERFORMANCE HELPER METHODS FROM GDSCRIPT
        bool is_material_cache_valid();
        void refresh_material_cache();
        ShaderMaterial* get_cached_material();
        bool values_equal(Variant a, Variant b);
        Vector2i get_target_viewport_size();
        Vector2i apply_lod_to_size(Vector2i target_size, Camera3D *active_cam);
        void invalidate_all_caches();

    protected:
        static void _bind_methods(); //This allows to bind methods to the Godot System

     //List of functions we want in this source file (we will define it's implementation in .cpp file)
    public:
        PlanarReflectorCPP(); //Constructor
        ~PlanarReflectorCPP(); //Destructor

        //List custom functions and/or built in from here
        void _process(double delta) override; 
        void _ready() override;
        void _exit_tree() override;
        void _notification(int what);
        
        bool is_active = true;
        Viewport* get_active_viewport();

        // //Setters and Getters - Required for Exported Variables Properties
        void set_is_active(bool p_active);
        bool get_is_active() const;
        
        // Core camera and resolution controls
        void set_game_main_camera(Camera3D *p_camera);
        Camera3D *get_game_main_camera() const;

        void set_editor_camera(Camera3D *p_camera);
        Camera3D *get_editor_camera() const;

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

        // Reflection Compositor Effects Group - NEW C++ INTEGRATION
        void set_use_custom_compositor(bool p_use_custom);
        bool get_use_custom_compositor() const;

        void set_custom_compositor(Compositor *p_compositor);
        Compositor *get_custom_compositor() const;

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