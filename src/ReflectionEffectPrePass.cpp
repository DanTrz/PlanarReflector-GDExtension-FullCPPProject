#include "ReflectionEffectPrePass.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

ReflectionEffectPrePass::ReflectionEffectPrePass() {
    set_effect_callback_type(CB_TYPE);
    
    // Initialize cache arrays
    cached_matrix_data.resize(32); // For both matrices (16 floats each)
    last_params.resize(PARAM_FLOATS);
    
    // Initialize on render thread
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        rs->call_on_render_thread(callable_mp(this, &ReflectionEffectPrePass::_initialize_compute));
    }
}

ReflectionEffectPrePass::~ReflectionEffectPrePass() {
    // Cleanup on render thread
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        rs->call_on_render_thread(callable_mp(this, &ReflectionEffectPrePass::_free_gpu));
    }
}

void ReflectionEffectPrePass::_notification(int p_what) {
    if (p_what == NOTIFICATION_PREDELETE) {
        _free_gpu();
    }
}

void ReflectionEffectPrePass::_initialize_compute() {
    rd = RenderingServer::get_singleton()->get_rendering_device();
    if (!rd) {
        return;
    }

    // Load and compile compute shader
    Ref<RDShaderFile> shader_file = ResourceLoader::get_singleton()->load("res://addons/PlanarReflectorCpp/SupportFiles/reflection_effect_prepass_compute.glsl");
    if (shader_file.is_valid()) {
        Ref<RDShaderSPIRV> spirv = shader_file->get_spirv();
        if (spirv.is_valid()) {
            shader = rd->shader_create_from_spirv(spirv);
        }
    }

    if (shader.is_valid()) {
        pipeline = rd->compute_pipeline_create(shader);
    }

    // Create samplers
    Ref<RDSamplerState> nearest_sampler_state;
    nearest_sampler_state.instantiate();
    nearest_sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
    nearest_sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
    nearest_sampler_state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
    sampler_rid = rd->sampler_create(nearest_sampler_state);

    Ref<RDSamplerState> linear_sampler_state;
    linear_sampler_state.instantiate();
    linear_sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    linear_sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    linear_sampler_state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    temp_sampler = rd->sampler_create(linear_sampler_state);

    // Create parameter buffer
    PackedFloat32Array data;
    data.resize(PARAM_FLOATS);
    PackedByteArray bytes = data.to_byte_array();
    parameter_storage_buffer = rd->storage_buffer_create(bytes.size(), bytes);
}

void ReflectionEffectPrePass::_free_gpu() {
    if (!rd) {
        return;
    }
    
    // Free cached uniform sets
    Array keys = cached_uniform_sets.keys();
    for (int i = 0; i < keys.size(); i++) {
        RID uniform_set_rid = cached_uniform_sets[keys[i]];
        if (uniform_set_rid.is_valid()) {
            rd->free_rid(uniform_set_rid);
        }
    }
    cached_uniform_sets.clear();
    
    if (temp_image.is_valid()) {
        rd->free_rid(temp_image);
        temp_image = RID();
    }
    if (temp_sampler.is_valid()) {
        rd->free_rid(temp_sampler);
        temp_sampler = RID();
    }
    if (sampler_rid.is_valid()) {
        rd->free_rid(sampler_rid);
        sampler_rid = RID();
    }
    if (parameter_storage_buffer.is_valid()) {
        rd->free_rid(parameter_storage_buffer);
        parameter_storage_buffer = RID();
    }
    if (pipeline.is_valid()) {
        rd->free_rid(pipeline);
        pipeline = RID();
    }
    if (shader.is_valid()) {
        rd->free_rid(shader);
        shader = RID();
    }
}

void ReflectionEffectPrePass::_ensure_temp_image(Vector2i size, RID like_color) {
    // Create a same-format storage+sampled image for the separable pass
    if (temp_image.is_valid()) {
        Ref<RDTextureFormat> info = rd->texture_get_format(temp_image);
        if (info->get_width() == size.x && info->get_height() == size.y) {
            return;
        }
        rd->free_rid(temp_image);
    }

    Ref<RDTextureFormat> fmt = rd->texture_get_format(like_color);
    Ref<RDTextureFormat> tf;
    tf.instantiate();
    tf->set_width(size.x);
    tf->set_height(size.y);
    tf->set_depth(1);
    tf->set_array_layers(1);
    tf->set_mipmaps(1);
    tf->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
    tf->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
    tf->set_format(fmt->get_format());
    tf->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
    
    Ref<RDTextureView> tv;
    tv.instantiate();
    
    // Create temp image - FIXED: Use TypedArray instead of PackedByteArray
    TypedArray<PackedByteArray> data_array;
    temp_image = rd->texture_create(tf, tv, data_array);
}

RID ReflectionEffectPrePass::_get_or_create_uniform_set(RID color_tex, RID depth_tex, int pass_type, RID temp_image_rid) {
    // Create a unique key for this combination
    String key = String::num_int64(color_tex.get_id()) + "_" + String::num_int64(depth_tex.get_id()) + "_" + String::num_int64(pass_type) + "_" + String::num_int64(temp_image_rid.get_id());
    
    if (cached_uniform_sets.has(key)) {
        return cached_uniform_sets[key];
    }
    
    // Create new uniform set
    Array uniforms;
    
    // Binding 0: Parameters buffer
    Ref<RDUniform> u_params;
    u_params.instantiate();
    u_params->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
    u_params->set_binding(0);
    u_params->add_id(parameter_storage_buffer);
    uniforms.push_back(u_params);

    // Binding 1: Color write image
    Ref<RDUniform> u_color_write;
    u_color_write.instantiate();
    u_color_write->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    u_color_write->set_binding(1);
    if (pass_type == 1) { // vertical pass writes to color
        u_color_write->add_id(color_tex);
    } else { // horizontal pass dummy binding
        u_color_write->add_id(temp_image_rid);
    }
    uniforms.push_back(u_color_write);

    // Binding 2: Depth sampler
    Ref<RDUniform> u_depth;
    u_depth.instantiate();
    u_depth->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
    u_depth->set_binding(2);
    u_depth->add_id(sampler_rid);
    u_depth->add_id(depth_tex);
    uniforms.push_back(u_depth);

    // Binding 3: Source color sampler
    Ref<RDUniform> u_src_color;
    u_src_color.instantiate();
    u_src_color->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
    u_src_color->set_binding(3);
    u_src_color->add_id(sampler_rid);
    u_src_color->add_id(color_tex);
    uniforms.push_back(u_src_color);

    // Binding 4: Temp image
    Ref<RDUniform> u_temp_image;
    u_temp_image.instantiate();
    u_temp_image->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    u_temp_image->set_binding(4);
    u_temp_image->add_id(temp_image_rid);
    uniforms.push_back(u_temp_image);

    // Binding 5: Temp sampler
    Ref<RDUniform> u_temp_sampler;
    u_temp_sampler.instantiate();
    u_temp_sampler->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
    u_temp_sampler->set_binding(5);
    u_temp_sampler->add_id(temp_sampler);
    u_temp_sampler->add_id(temp_image_rid);
    uniforms.push_back(u_temp_sampler);

    RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
    cached_uniform_sets[key] = uniform_set;
    return uniform_set;
}

bool ReflectionEffectPrePass::_update_params_if_changed(PackedFloat32Array new_params) {
    // Quick check if parameters have changed
    if (last_params.size() == new_params.size()) {
        bool changed = false;
        for (int i = 0; i < new_params.size(); i++) {
            if (Math::abs(last_params[i] - new_params[i]) > 0.0001) { // Small epsilon for float comparison
                changed = true;
                break;
            }
        }
        if (!changed) {
            return false;
        }
    }
    
    // Parameters changed, update buffer
    PackedByteArray bytes = new_params.to_byte_array();
    rd->buffer_update(parameter_storage_buffer, 0, bytes.size(), bytes);
    last_params = new_params;
    return true;
}

bool ReflectionEffectPrePass::_cache_matrix_data(Projection inv_proj, Transform3D cam_xform) {
    // Check if matrices have changed significantly
    bool proj_changed = false;
    bool transform_changed = false;
    
    if (last_inv_proj_matrix != inv_proj) {
        last_inv_proj_matrix = inv_proj;
        proj_changed = true;
        
        // Cache inverse projection matrix data
        cached_matrix_data[0]  = inv_proj.columns[0][0]; cached_matrix_data[1]  = inv_proj.columns[0][1];
        cached_matrix_data[2]  = inv_proj.columns[0][2]; cached_matrix_data[3]  = inv_proj.columns[0][3];
        cached_matrix_data[4]  = inv_proj.columns[1][0]; cached_matrix_data[5]  = inv_proj.columns[1][1];
        cached_matrix_data[6]  = inv_proj.columns[1][2]; cached_matrix_data[7]  = inv_proj.columns[1][3];
        cached_matrix_data[8]  = inv_proj.columns[2][0]; cached_matrix_data[9]  = inv_proj.columns[2][1];
        cached_matrix_data[10] = inv_proj.columns[2][2]; cached_matrix_data[11] = inv_proj.columns[2][3];
        cached_matrix_data[12] = inv_proj.columns[3][0]; cached_matrix_data[13] = inv_proj.columns[3][1];
        cached_matrix_data[14] = inv_proj.columns[3][2]; cached_matrix_data[15] = inv_proj.columns[3][3];
    }
    
    if (!last_cam_transform.is_equal_approx(cam_xform)) {
        last_cam_transform = cam_xform;
        transform_changed = true;
        
        // Cache camera transform data
        Basis basis = cam_xform.basis;
        Vector3 origin = cam_xform.origin;
        
        cached_matrix_data[16] = basis.rows[0][0]; cached_matrix_data[17] = basis.rows[0][1];
        cached_matrix_data[18] = basis.rows[0][2]; cached_matrix_data[19] = 0.0;
        cached_matrix_data[20] = basis.rows[1][0]; cached_matrix_data[21] = basis.rows[1][1];
        cached_matrix_data[22] = basis.rows[1][2]; cached_matrix_data[23] = 0.0;
        cached_matrix_data[24] = basis.rows[2][0]; cached_matrix_data[25] = basis.rows[2][1];
        cached_matrix_data[26] = basis.rows[2][2]; cached_matrix_data[27] = 0.0;
        cached_matrix_data[28] = origin.x;         cached_matrix_data[29] = origin.y;
        cached_matrix_data[30] = origin.z;         cached_matrix_data[31] = 1.0;
    }
    
    return proj_changed || transform_changed;
}

void ReflectionEffectPrePass::_render_callback(EffectCallbackType p_effect_callback_type, RenderData *p_render_data) {
    if (p_effect_callback_type != CB_TYPE) {
        return;
    }
    if (!effect_enabled) {
        return;
    }
    if (!rd || !shader.is_valid() || !pipeline.is_valid() || !sampler_rid.is_valid()) {
        return;
    }

    // FIXED: Use Ref<RenderSceneBuffers> and cast to RenderSceneBuffersRD
    Ref<RenderSceneBuffers> rsb_ref = p_render_data->get_render_scene_buffers();
    if (!rsb_ref.is_valid()) {
        return;
    }
    
    // Cast to RenderSceneBuffersRD to access extended methods
    RenderSceneBuffersRD *rsb = Object::cast_to<RenderSceneBuffersRD>(rsb_ref.ptr());
    if (!rsb) {
        return;
    }

    Vector2i size = rsb->get_internal_size();
    if (size.x == 0 || size.y == 0) {
        return;
    }

    int x_groups = (size.x + 7) / 8;
    int y_groups = (size.y + 7) / 8;

    int view_count = rsb->get_view_count();
    for (int view = 0; view < view_count; view++) {
        RID color_tex = rsb->get_color_layer(view);
        RID depth_tex = rsb->get_depth_layer(view);
        if (!color_tex.is_valid() || !depth_tex.is_valid()) {
            continue;
        }

        _ensure_temp_image(size, color_tex);

        // Base params (shared by both passes)
        PackedFloat32Array params;
        params.resize(PARAM_FLOATS);
        params[0] = static_cast<float>(size.x);
        params[1] = static_cast<float>(size.y);
        params[2] = static_cast<float>(intersect_height);
        params[3] = static_cast<float>(reflect_gap_fill);

        // Get matrices from render data
        Projection inv_proj = p_render_data->get_render_scene_data()->get_cam_projection().inverse();
        Transform3D cam_xform = p_render_data->get_render_scene_data()->get_cam_transform();
        
        _cache_matrix_data(inv_proj, cam_xform);
        
        // Copy cached matrix data to params array
        for (int i = 0; i < 32; i++) {
            params[4 + i] = cached_matrix_data[i];
        }

        params[36] = fill_enabled ? 1.0f : 0.0f;
        params[37] = static_cast<float>(fill_radius_px);
        params[38] = Math::clamp(static_cast<float>(fill_aggressiveness), 0.0f, 1.0f);

        // ---------- PASS 1: horizontal -> write temp_image ----------
        params[39] = 0.0f;  // pass_dir = horizontal
        
        _update_params_if_changed(params);
        
        RID uniform_set_h = _get_or_create_uniform_set(color_tex, depth_tex, 0, temp_image);

        int64_t cl1 = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(cl1, pipeline);
        rd->compute_list_bind_uniform_set(cl1, uniform_set_h, 0);
        rd->compute_list_dispatch(cl1, x_groups, y_groups, 1);
        rd->compute_list_end();

        // ---------- PASS 2: vertical -> read temp, write color ----------
        params[39] = 1.0f;  // pass_dir = vertical
        
        PackedByteArray bytes2 = params.to_byte_array();
        rd->buffer_update(parameter_storage_buffer, 0, bytes2.size(), bytes2);

        RID uniform_set_v = _get_or_create_uniform_set(color_tex, depth_tex, 1, temp_image);

        int64_t cl2 = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(cl2, pipeline);
        rd->compute_list_bind_uniform_set(cl2, uniform_set_v, 0);
        rd->compute_list_dispatch(cl2, x_groups, y_groups, 1);
        rd->compute_list_end();
    }
}

void ReflectionEffectPrePass::_bind_methods() {
    // Property bindings
    ClassDB::bind_method(D_METHOD("set_effect_enabled", "enabled"), &ReflectionEffectPrePass::set_effect_enabled);
    ClassDB::bind_method(D_METHOD("get_effect_enabled"), &ReflectionEffectPrePass::get_effect_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "effect_enabled"), "set_effect_enabled", "get_effect_enabled");

    ClassDB::bind_method(D_METHOD("set_intersect_height", "height"), &ReflectionEffectPrePass::set_intersect_height);
    ClassDB::bind_method(D_METHOD("get_intersect_height"), &ReflectionEffectPrePass::get_intersect_height);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intersect_height"), "set_intersect_height", "get_intersect_height");

    ClassDB::bind_method(D_METHOD("set_reflect_gap_fill", "gap_fill"), &ReflectionEffectPrePass::set_reflect_gap_fill);
    ClassDB::bind_method(D_METHOD("get_reflect_gap_fill"), &ReflectionEffectPrePass::get_reflect_gap_fill);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflect_gap_fill"), "set_reflect_gap_fill", "get_reflect_gap_fill");

    ClassDB::bind_method(D_METHOD("set_fill_enabled", "enabled"), &ReflectionEffectPrePass::set_fill_enabled);
    ClassDB::bind_method(D_METHOD("get_fill_enabled"), &ReflectionEffectPrePass::get_fill_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fill_enabled"), "set_fill_enabled", "get_fill_enabled");

    ClassDB::bind_method(D_METHOD("set_fill_radius_px", "radius"), &ReflectionEffectPrePass::set_fill_radius_px);
    ClassDB::bind_method(D_METHOD("get_fill_radius_px"), &ReflectionEffectPrePass::get_fill_radius_px);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fill_radius_px", PROPERTY_HINT_RANGE, "1,96,1"), "set_fill_radius_px", "get_fill_radius_px");

    ClassDB::bind_method(D_METHOD("set_fill_aggressiveness", "aggressiveness"), &ReflectionEffectPrePass::set_fill_aggressiveness);
    ClassDB::bind_method(D_METHOD("get_fill_aggressiveness"), &ReflectionEffectPrePass::get_fill_aggressiveness);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fill_aggressiveness", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_fill_aggressiveness", "get_fill_aggressiveness");

    // FIXED: Remove GDVIRTUAL_BIND as this causes the undefined identifier error
    // The virtual method is automatically detected by Godot's binding system
}

// Property implementations
void ReflectionEffectPrePass::set_effect_enabled(bool p_enabled) {
    effect_enabled = p_enabled;
}

bool ReflectionEffectPrePass::get_effect_enabled() const {
    return effect_enabled;
}

void ReflectionEffectPrePass::set_intersect_height(double p_height) {
    intersect_height = p_height;
}

double ReflectionEffectPrePass::get_intersect_height() const {
    return intersect_height;
}

void ReflectionEffectPrePass::set_reflect_gap_fill(double p_gap_fill) {
    reflect_gap_fill = p_gap_fill;
}

double ReflectionEffectPrePass::get_reflect_gap_fill() const {
    return reflect_gap_fill;
}

void ReflectionEffectPrePass::set_fill_enabled(bool p_enabled) {
    fill_enabled = p_enabled;
}

bool ReflectionEffectPrePass::get_fill_enabled() const {
    return fill_enabled;
}

void ReflectionEffectPrePass::set_fill_radius_px(double p_radius) {
    fill_radius_px = Math::clamp(p_radius, 1.0, 96.0);
}

double ReflectionEffectPrePass::get_fill_radius_px() const {
    return fill_radius_px;
}

void ReflectionEffectPrePass::set_fill_aggressiveness(double p_aggressiveness) {
    fill_aggressiveness = Math::clamp(p_aggressiveness, 0.0, 2.0);
}

double ReflectionEffectPrePass::get_fill_aggressiveness() const {
    return fill_aggressiveness;
}