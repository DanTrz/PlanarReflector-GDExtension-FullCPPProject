#ifndef PLANAR_REFLECTOR_CLIPPING_CPP_H
#define PLANAR_REFLECTOR_CLIPPING_CPP_H

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace godot {

class PlanarReflectorClippingCPP : public MeshInstance3D {
    GDCLASS(PlanarReflectorClippingCPP, MeshInstance3D)

private:
    // All enabled clipping reflectors share one camera-selected height and one
    // last-modified marker. The ObjectID registry never owns node lifetimes.
    static ObjectID last_height_writer_id;
    static ObjectID last_marker_writer_id;
    static bool global_parameters_initialized;
    static Vector<ObjectID> clipping_participants;
    static double shared_water_height;
    static uint32_t shared_marker_bit;
    static bool shared_height_initialized;
    static bool shared_marker_initialized;
    static ObjectID selected_reflector_id;
    static uint64_t last_selection_frame;
    static bool selection_dirty;
    static bool selection_camera_cache_valid;
    static ObjectID selection_camera_id;
    static Transform3D selection_camera_transform;
    static int selection_camera_projection;
    static double selection_camera_fov;
    static double selection_camera_size;
    static double selection_camera_near;
    static double selection_camera_far;
    static double selection_camera_h_offset;
    static double selection_camera_v_offset;
    static Vector2 selection_camera_frustum_offset;
    static int selection_camera_keep_aspect;
    static uint32_t selection_camera_cull_mask;
    static Vector2i selection_camera_viewport_size;
    static constexpr const char *WATER_HEIGHT_GLOBAL = "planar_water_height";
    static constexpr const char *CAMERA_BIT_GLOBAL = "planar_reflection_camera_bit";

    Camera3D *main_camera = nullptr;
    ObjectID main_camera_id; // Recovers the exported camera after a tree re-entry (req 10.4).
    Camera3D *editor_camera = nullptr;
    Camera3D *reflection_camera = nullptr;
    SubViewport *reflection_viewport = nullptr;

    bool is_active = true;
    bool global_water_clipping_enabled = false;
    bool debug_selection_diagnostics = false;
    int reflection_camera_marker_layer = 20;
    int reflection_layers = 1;

    bool auto_detect_camera_mode = true;
    double ortho_scale_multiplier = 1.0;
    Vector2i fixed_reflection_resolution = Vector2i(1280, 720);
    int resolution_mode = 0; // Match viewport, scaled viewport, fixed.
    double resolution_scale = 0.5;
    int update_frequency = 1;
    int shadow_atlas_size = 2048;

    bool use_custom_environment = false;
    Ref<Environment> custom_environment;

    bool enable_reflection_offset = false;
    Vector3 reflection_offset_position;
    Vector3 reflection_offset_rotation;
    double reflection_offset_scale = 1.0;
    int offset_blend_mode = 0;

    bool use_lod = false;
    double lod_distance_near = 20.0;
    double lod_distance_far = 60.0;
    double lod_resolution_multiplier = 0.5;

    int frame_counter = 0;
    int setup_generation = 0;
    bool setup_complete = false;
    Vector2i last_viewport_size;
    Ref<ShaderMaterial> bound_reflector_material;
    bool editor_texture_binding_suspended = false;

    // Last-applied reflection camera state; properties are written only when the
    // source value changed (req 12.7).
    int last_projection_type = -1;
    double last_fov = -1.0;
    double last_ortho_size = -1.0;
    double last_near = -1.0;
    double last_far = -1.0;
    int last_keep_aspect = -1;
    double last_h_offset = 1.0e30;
    double last_v_offset = 1.0e30;
    Vector2 last_frustum_offset = Vector2(1.0e30, 1.0e30);
    uint32_t last_cull_mask = 0xFFFFFFFFu;

    // Diagnostic state (req 16.4/16.5) - set on events, read by configuration warnings.
    bool global_parameter_conflict = false;
    bool global_conflict_warned = false;
    bool missing_camera_warned = false;

    void schedule_setup();
    void deferred_setup(int p_generation);
    bool ensure_initialized();
    void create_runtime_nodes();
    void destroy_runtime_nodes();
    void apply_activity_state();
    void resolve_editor_camera();
    void configure_reflection_camera();
    void sync_projection(Camera3D *p_source);
    void sync_viewport_size(Camera3D *p_source);
    void sync_reflection_transform(Camera3D *p_source);
    void sync_all(Camera3D *p_source);
    void bind_reflection_texture();
    void clear_reflection_texture();
    void restore_reflection_texture_after_save();
    void request_reflection_render();
    void setup_environment();

    void _on_main_camera_exiting();
    void _on_main_camera_entered();
    void _on_editor_camera_exiting();
    void warn_missing_runtime_camera();

    Camera3D *get_source_camera() const;
    uint32_t get_marker_bit() const;
    uint32_t get_effective_reflection_mask() const;
    Vector2i calculate_target_size(Camera3D *p_source) const;
    Transform3D apply_reflection_offset_to(const Transform3D &p_transform) const;

    bool ensure_global_parameters();
    void register_clipping_participant();
    void unregister_clipping_participant();
    void publish_global_height(bool p_force = false);
    void publish_global_marker(bool p_force = false);
    static void refresh_participant_cameras_and_warnings();
    static void update_active_reflector_selection(Camera3D *p_source_camera, bool p_force = false);
    static bool aabb_intersects_camera_frustum(const AABB &p_world_aabb, Camera3D *p_camera);
    static double distance_squared_to_aabb(const Vector3 &p_point, const AABB &p_aabb);
    static bool is_selection_debug_enabled();
    static void update_selection_debug_visuals();

protected:
    static void _bind_methods();

public:
    // Called once from register_types.cpp at module init (SCENE level): the global
    // uniforms must exist before any scene shader that references them compiles,
    // which is long before any node's setup runs.
    static void ensure_global_shader_parameters();
    static void shutdown_shared_manager();

    PlanarReflectorClippingCPP();
    ~PlanarReflectorClippingCPP();

    void _process(double p_delta) override;
    void _exit_tree() override;
    void _notification(int p_what);
    PackedStringArray _get_configuration_warnings() const override;

    void set_is_active(bool p_value);
    bool get_is_active() const;
    void set_main_camera(Camera3D *p_camera);
    Camera3D *get_main_camera() const;

    void set_global_water_clipping_enabled(bool p_value);
    bool get_global_water_clipping_enabled() const;
    void set_debug_selection_diagnostics(bool p_value);
    bool get_debug_selection_diagnostics() const;
    void set_reflection_camera_marker_layer(int p_layer);
    int get_reflection_camera_marker_layer() const;

    void set_reflection_layers(int p_layers);
    int get_reflection_layers() const;
    void set_auto_detect_camera_mode(bool p_value);
    bool get_auto_detect_camera_mode() const;
    void set_ortho_scale_multiplier(double p_value);
    double get_ortho_scale_multiplier() const;

    void set_resolution_mode(int p_mode);
    int get_resolution_mode() const;
    void set_resolution_scale(double p_scale);
    double get_resolution_scale() const;
    void set_fixed_reflection_resolution(Vector2i p_size);
    Vector2i get_fixed_reflection_resolution() const;
    void set_update_frequency(int p_frequency);
    int get_update_frequency() const;
    void set_shadow_atlas_size(int p_size);
    int get_shadow_atlas_size() const;

    void set_use_custom_environment(bool p_value);
    bool get_use_custom_environment() const;
    void set_custom_environment(Environment *p_environment);
    Environment *get_custom_environment() const;
    void set_enable_reflection_offset(bool p_value);
    bool get_enable_reflection_offset() const;
    void set_reflection_offset_position(Vector3 p_value);
    Vector3 get_reflection_offset_position() const;
    void set_reflection_offset_rotation(Vector3 p_value);
    Vector3 get_reflection_offset_rotation() const;
    void set_reflection_offset_scale(double p_value);
    double get_reflection_offset_scale() const;
    void set_offset_blend_mode(int p_mode);
    int get_offset_blend_mode() const;

    void set_use_lod(bool p_value);
    bool get_use_lod() const;
    void set_lod_distance_near(double p_value);
    double get_lod_distance_near() const;
    void set_lod_distance_far(double p_value);
    double get_lod_distance_far() const;
    void set_lod_resolution_multiplier(double p_value);
    double get_lod_resolution_multiplier() const;
};

} // namespace godot

#endif
