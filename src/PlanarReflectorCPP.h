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
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/basis.hpp>

namespace godot {

    class PlanarReflectorCPP : public MeshInstance3D 
    {
        GDCLASS(PlanarReflectorCPP, MeshInstance3D)
    
    //Define Variables/functions => group variables by permission (Private, protected, etc)
    private:
        // Core reflection components
        Camera3D *main_camera = nullptr;
        Camera3D *reflect_camera = nullptr;
        SubViewport *reflect_viewport = nullptr;
        
        // Resolution and camera controls
        Vector2i reflection_camera_resolution = Vector2i(1920, 1080);
        double ortho_scale_multiplier = 1.0;
        double ortho_uv_scale = 1.0;
        bool auto_detect_camera_mode = true;

        // Layer and environment control
        int reflection_layers = 1; // Using int for flags_3d_render
        bool use_custom_environment = true;
        Environment *custom_environment = nullptr;

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

        // Internal optimization variables
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

        // Internal helper methods
        void setup_reflection_viewport();
        void setup_reflection_camera();
        void setup_reflection_layers();
        void setup_reflection_environment();
        Plane calculate_reflection_plane();
        void update_reflection_camera();
        void update_camera_projection();
        void update_viewport();
        void update_shader_parameters();
        Transform3D apply_reflection_offset(const Transform3D &base_transform);
        void update_offset_cache();
        bool should_update_reflection();

    protected:
        static void _bind_methods(); //This allows to bind methods to the Godot System

     //List of functions we want in this source file (we will define it's implementation in .cpp file)
    public:
        PlanarReflectorCPP(); //Constructor
        ~PlanarReflectorCPP(); //Destructor

        //List custom functions and/or built in from here
        void _process(double delta) override; 
        void _ready() override;  

        //Setters and Getters - Required for Exported Variables Properties
        
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