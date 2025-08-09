#include "register_types.h"
#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "gfgd_scene_tree.h"
#include "game_instance.h"
#include "game_mode.h"
#include "game_mode_settings.h"
#include "level.h"

using namespace godot;
using namespace GFGD;

void initialize_gfgd_module(ModuleInitializationLevel p_level)
{
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(GFGDSceneTree);
	GDREGISTER_CLASS(GameInstance);
	GDREGISTER_CLASS(GameMode);
	GDREGISTER_CLASS(GameModeSettings);
	GDREGISTER_CLASS(Level);
}

void uninitialize_gfgd_module(ModuleInitializationLevel p_level)
{
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C"
{
	GDExtensionBool GDE_EXPORT example_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
	{
		godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

		init_obj.register_initializer(initialize_gfgd_module);
		init_obj.register_terminator(uninitialize_gfgd_module);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}