#include "register_types.h"
#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "gfgd_scene_tree.h"
#include "game_instance.h"
#include "game_mode.h"
#include "game_mode_settings.h"
#include "level.h"
#include "controller.h"
#include "pawn_handler.h"
#include "input_component.h"
#include "player_controller.h"
#include "ipawn.h"
#include "node_component.h"
#include "save_game.h"
#include "assert.h"
#include "assertion_exception.h"
#include "assertion_messages.h"
#include "project_statics.h"
#include "gameplay_tag.h"
#include "gameplay_tag_container.h"
#include "gameplay_tag_count_container.h"
#include "gameplay_tags_manager.h"
#include "ability_system_component.h"
#include "gameplay_ability.h"
#include "gameplay_effect.h"

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
	GDREGISTER_CLASS(IPawn);
	GDREGISTER_CLASS(NodeComponent);
	GDREGISTER_CLASS(SaveGame);
	GDREGISTER_CLASS(Assert);
	GDREGISTER_CLASS(AssertionException);
	GDREGISTER_CLASS(AssertionMessages);
	GDREGISTER_CLASS(ProjectStatics);
	GDREGISTER_CLASS(GameplayTag);
	GDREGISTER_CLASS(GameplayTagContainer);
	GDREGISTER_CLASS(GameplayTagCountContainer);
	GDREGISTER_CLASS(GameplayTagsManager);
	GDREGISTER_CLASS(AbilitySystemComponent);
	GDREGISTER_CLASS(GameplayAbility);
	GDREGISTER_CLASS(GameplayEffect);
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