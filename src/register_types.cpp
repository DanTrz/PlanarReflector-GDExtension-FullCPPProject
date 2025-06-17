#include "register_types.h"

//Include the other headers you want to register with Godot

//your custom header files created for the project
#include "PlanarReflectorCPP.h"

//your Godot and GDExtensions base classes
#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

//Implement the methods from our register Header file
void initialize_planar_reflector_cpp(ModuleInitializationLevel p_level) 
{
    //Default when you want this to be registered and loaded
    //Read Godot docs:
    //MODULE_INITIALIZATION_LEVEL_SCENE - When the scene is loaded (for nodes or other scene objects)
    //MODULE_INITIALIZATION_LEVEL_CORE - When godot loads (for plugins)
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) 
    {
        return;
    }

    // Register the custom class with to Godot.
    ClassDB::register_class<PlanarReflectorCPP>();
}

void unitialize_planar_reflector_cpp(ModuleInitializationLevel p_level) 
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) 
    {
        return;
    }
    //Usually mostly need to cleanup resources, or disconnect signals, etc
}

extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT planar_reflector_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_planar_reflector_cpp);
	init_obj.register_terminator(unitialize_planar_reflector_cpp);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}

