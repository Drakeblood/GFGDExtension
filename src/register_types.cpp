#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include "framework/gfgd_scene_tree.h"
#include "framework/game_instance.h"
#include "framework/player_input.h"
#include "framework/local_player.h"
#include "framework/input_router.h"
#include "framework/game_state.h"
#include "framework/player_state.h"
#include "framework/game_mode.h"
#include "framework/game_mode_settings.h"
#include "framework/level.h"
#include "framework/controller.h"
#include "framework/pawn_handler.h"
#include "framework/input_component.h"
#include "framework/player_controller.h"
#include "framework/save_game.h"
#include "core/assert.h"
#include "core/assertion_exception.h"
#include "core/assertion_messages.h"
#include "core/project_statics.h"
#include "gameplay_tags/gameplay_tag.h"
#include "gameplay_tags/gameplay_tag_container.h"
#include "gameplay_tags/gameplay_tag_count_container.h"
#include "gameplay_tags/gameplay_tag_table.h"
#include "gameplay_tags/gameplay_tags_manager.h"
#include "ability_system/attribute_set.h"
#include "ability_system/attribute_modifier.h"
#include "ability_system/ability_system_component.h"
#include "ability_system/gameplay_ability.h"
#include "ability_system/gameplay_effect.h"
#include "ability_system/active_gameplay_effect.h"

#ifdef TOOLS_ENABLED
#include "editor/gameplay_tag_editor_property.h"
#include "editor/gameplay_tag_picker_popup.h"
#include "editor/gameplay_tag_table_editor_property.h"
#include "editor/gameplay_tag_tree.h"
#include "editor/gameplay_tag_container_editor_property.h"
#include "editor/gameplay_tags_editor_inspector.h"
#include "editor/gameplay_tags_editor_plugin.h"
#endif

using namespace godot;
using namespace GFGD;

static void register_gfgd_setting(const String& name, const Variant& default_value, Variant::Type type, PropertyHint hint = PROPERTY_HINT_NONE, const String& hint_string = "")
{
	ProjectSettings* project_settings = ProjectSettings::get_singleton();
	if (!project_settings->has_setting(name))
	{
		project_settings->set_setting(name, default_value);
	}
	project_settings->set_initial_value(name, default_value);

	Dictionary property_info;
	property_info["name"] = name;
	property_info["type"] = type;
	property_info["hint"] = hint;
	property_info["hint_string"] = hint_string;
	project_settings->add_property_info(property_info);
	project_settings->set_as_basic(name, true);
}

static void register_gfgd_settings()
{
	register_gfgd_setting("application/game_framework/game_instance_script", String(), Variant::STRING, PROPERTY_HINT_FILE, "*.gd,*.cs");
	register_gfgd_setting("application/game_framework/default_game_mode_settings", String(), Variant::STRING, PROPERTY_HINT_FILE, "*.tres,*.res");
	// Every element is a path to a GameplayTagTable; the order is the merge
	// order, and on a duplicate tag the table listed first wins.
	register_gfgd_setting("application/game_framework/gameplay_tag_tables", PackedStringArray(), Variant::PACKED_STRING_ARRAY,
		PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:*.tres,*.res", Variant::STRING, PROPERTY_HINT_FILE));
}

void initialize_gdextension_types(ModuleInitializationLevel p_level)
{
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
#ifdef TOOLS_ENABLED
		GDREGISTER_INTERNAL_CLASS(GameplayTagTree);
		GDREGISTER_INTERNAL_CLASS(GameplayTagPickerPopup);
		GDREGISTER_INTERNAL_CLASS(GameplayTagEditorProperty);
		GDREGISTER_INTERNAL_CLASS(GameplayTagContainerEditorProperty);
		GDREGISTER_INTERNAL_CLASS(GameplayTagTableEditorProperty);
		GDREGISTER_INTERNAL_CLASS(GameplayTagsEditorInspector);
		GDREGISTER_INTERNAL_CLASS(GameplayTagsEditorPlugin);
		EditorPlugins::add_by_type<GameplayTagsEditorPlugin>();
#endif
		return;
	}

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	register_gfgd_settings();

	GDREGISTER_CLASS(GFGDSceneTree);
	GDREGISTER_CLASS(PlayerInput);
	GDREGISTER_CLASS(LocalPlayer);
	GDREGISTER_CLASS(InputRouter);
	GDREGISTER_CLASS(GameInstance);
	GDREGISTER_CLASS(GameModeSettings);
	GDREGISTER_CLASS(GameMode);
	GDREGISTER_CLASS(Level);
	GDREGISTER_CLASS(PlayerState);
	GDREGISTER_CLASS(GameState);
	GDREGISTER_CLASS(Controller);
	GDREGISTER_CLASS(PlayerController);
	GDREGISTER_CLASS(PawnHandler);
	GDREGISTER_CLASS(InputComponent);
	GDREGISTER_CLASS(SaveGame);
	GDREGISTER_CLASS(Assert);
	GDREGISTER_CLASS(AssertionException);
	GDREGISTER_CLASS(AssertionMessages);
	GDREGISTER_CLASS(ProjectStatics);
	GDREGISTER_CLASS(GameplayTag);
	GDREGISTER_CLASS(GameplayTagContainer);
	GDREGISTER_CLASS(GameplayTagCountContainer);
	GDREGISTER_CLASS(GameplayTagTable);
	GDREGISTER_CLASS(GameplayTagsManager);
	GDREGISTER_CLASS(AttributeSet);
	GDREGISTER_CLASS(AttributeModifier);
	GDREGISTER_CLASS(GameplayEffect);
	GDREGISTER_CLASS(ActiveGameplayEffect);
	GDREGISTER_CLASS(GameplayAbility);
	GDREGISTER_CLASS(AbilitySystemComponent);
}

void uninitialize_gdextension_types(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
#ifdef TOOLS_ENABLED
		EditorPlugins::remove_by_type<GameplayTagsEditorPlugin>();
#endif
		return;
	}

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GameplayTagsManager::destroy_singleton();
}

extern "C"
{
	// Initialization
	GDExtensionBool GDE_EXPORT gfgd_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
	{
		GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
		init_obj.register_initializer(initialize_gdextension_types);
		init_obj.register_terminator(uninitialize_gdextension_types);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
