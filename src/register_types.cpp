#include "register_types.h"

//Include the other headers you want to register with Godot
#include "PlanarReflectorCPP.h"
#include "ReflectionEffectPrePass.h"
#include "PlanarReflectorCPP2.h"


//your Godot and GDExtensions base classes
#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

// SINGLE INITIALIZATION FUNCTION - Register both classes at SCENE level
void initialize_planar_reflector_types(ModuleInitializationLevel p_level) 
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) 
    {
        return;
    }

    // Register both classes at SCENE level
    ClassDB::register_class<PlanarReflectorCPP>();
    ClassDB::register_class<ReflectionEffectPrePass>();
    ClassDB::register_class<PlanarReflectorCPP2>();

    
    // UtilityFunctions::print("Both PlanarReflectorCPP and ReflectionEffectPrePass registered at SCENE level");
}

void unitialize_planar_reflector_types(ModuleInitializationLevel p_level) 
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) 
    {
        return;
    }
    // Cleanup both classes
}

extern "C" {
// Single entry point for both classes
GDExtensionBool GDE_EXPORT planar_reflector_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	// Register single initializer for both classes
	init_obj.register_initializer(initialize_planar_reflector_types);
	init_obj.register_terminator(unitialize_planar_reflector_types);
	
	// Set to SCENE level - this covers both classes
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
