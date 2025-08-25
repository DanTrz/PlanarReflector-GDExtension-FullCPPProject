#ifndef REFLECTION_EFFECT_PREPASS_H
#define REFLECTION_EFFECT_PREPASS_H

#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/render_scene_buffers.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/render_scene_data.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

class ReflectionEffectPrePass : public CompositorEffect {
    GDCLASS(ReflectionEffectPrePass, CompositorEffect)

private:
    // Core effect parameters
    bool effect_enabled = true;
    double intersect_height = 0.0;
    double reflect_gap_fill = 0.0025;
    
    // Hole handling parameters
    bool fill_enabled = true;
    double fill_radius_px = 24.0;
    double fill_aggressiveness = 1.0;
    
    // RD resources
    RenderingDevice *rd = nullptr;
    RID shader;
    RID pipeline;
    RID sampler_rid;
    RID parameter_storage_buffer;
    RID temp_image;
    RID temp_sampler;
    
    // Performance optimization caches
    Dictionary cached_uniform_sets;
    PackedFloat32Array last_params;
    PackedFloat32Array cached_matrix_data;
    Projection last_inv_proj_matrix;
    Transform3D last_cam_transform;
    
    // Constants
    static const int PARAM_FLOATS = 40;
    static const EffectCallbackType CB_TYPE = EFFECT_CALLBACK_TYPE_POST_OPAQUE;
    
    // Internal methods
    void _initialize_compute();
    void _free_gpu();
    void _ensure_temp_image(Vector2i size, RID like_color);
    RID _get_or_create_uniform_set(RID color_tex, RID depth_tex, int pass_type, RID temp_image_rid);
    bool _update_params_if_changed(PackedFloat32Array new_params);
    bool _cache_matrix_data(Projection inv_proj, Transform3D cam_xform);

public:
    ReflectionEffectPrePass();
    ~ReflectionEffectPrePass();

    // Virtual method implementation - FIXED: No override since this is the base virtual method
    virtual void _render_callback(EffectCallbackType p_effect_callback_type, RenderData *p_render_data);

    // Property setters and getters
    void set_effect_enabled(bool p_enabled);
    bool get_effect_enabled() const;
    
    void set_intersect_height(double p_height);
    double get_intersect_height() const;
    
    void set_reflect_gap_fill(double p_gap_fill);
    double get_reflect_gap_fill() const;
    
    void set_fill_enabled(bool p_enabled);
    bool get_fill_enabled() const;
    
    void set_fill_radius_px(double p_radius);
    double get_fill_radius_px() const;
    
    void set_fill_aggressiveness(double p_aggressiveness);
    double get_fill_aggressiveness() const;

protected:
    static void _bind_methods();
    void _notification(int p_what);
};

}

#endif // REFLECTION_EFFECT_PREPASS_H