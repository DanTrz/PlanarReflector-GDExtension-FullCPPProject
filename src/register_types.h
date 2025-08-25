#ifndef PLANAR_REFLECTOR_REGISTER_TYPES_H
#define PLANAR_REFLECTOR_REGISTER_TYPES_H

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

// Single registration function for both classes
void initialize_planar_reflector_types(ModuleInitializationLevel p_level);
void unitialize_planar_reflector_types(ModuleInitializationLevel p_level);

#endif