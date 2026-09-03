#include "core/project_statics.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/gd_script.hpp>

#include "framework/world.h"
#include "framework/game_instance.h"
#include "framework/game_mode_base.h"
#include "framework/game_state_base.h"
#include "framework/level.h"
#include "framework/local_player.h"
#include "framework/player_controller.h"
#include "framework/save_game.h"

using namespace godot;

namespace GFGD
{
// Plain char constants: godot::String statics would run their constructors
// during DLL load, before the GDExtension interface is initialized.
static const char* USER_LOCATION = "user://";
static const char* SAVES_FOLDER = "saves/";
static const char* SAVES_LOCATION = "user://saves/";
static const char* SAVE_GAME_ENCRYPTION_KEY = "super_secret_password";
static const char* SAVE_KEY_SETTING = "application/game_framework/save_encryption_key";

// The historic key is compiled into every copy of this extension, so a shipped
// game that keeps it is encrypted against nobody. The default stays as it was -
// changing it would make existing saves unreadable - but a project is told once
// per run that it is still using it.
static String resolve_encryption_key()
{
	static bool warned_about_default = false;

	const String key = ProjectSettings::get_singleton()->get_setting(SAVE_KEY_SETTING, String(SAVE_GAME_ENCRYPTION_KEY));
	if (key.is_empty()) { return String(SAVE_GAME_ENCRYPTION_KEY); }

	if (!warned_about_default && key == String(SAVE_GAME_ENCRYPTION_KEY))
	{
		warned_about_default = true;
		WARN_PRINT("GFGD: save files use the framework's default encryption key, which ships with the source. Set application/game_framework/save_encryption_key before release.");
	}

	return key;
}

void ProjectStatics::save_game(const String& slot_name, const Ref<SaveGame>& data, bool encrypt)
{
	if (data.is_null()) return;

	Ref<DirAccess> dir = DirAccess::open(USER_LOCATION);
	if (dir.is_null()) return;

	if (!dir->dir_exists(SAVES_FOLDER))
	{
		Error err = dir->make_dir(SAVES_FOLDER);
		if (err != OK)
		{
			print_error("Failed to create saves directory");
			return;
		}
	}

	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(data->to_json());
	if (err != OK)
	{
		print_error("Failed to serialize save game data");
		return;
	}

	String json_string = json->get_data().stringify();

	Ref<FileAccess> file;
	if (encrypt)
	{
		file = FileAccess::open_encrypted_with_pass(SAVES_LOCATION + slot_name + ".sav", FileAccess::ModeFlags::WRITE, resolve_encryption_key());
	}
	else
	{
		file = FileAccess::open(SAVES_LOCATION + slot_name + ".sav", FileAccess::ModeFlags::WRITE);
	}

	if (file.is_null())
	{
		print_error("Failed to open save file for writing");
		return;
	}

	file->store_string(json_string);
	file->close();
	print_line("Game saved successfully.");
}

// Deliberately not try_create_instance_from(): that returns a raw pointer, which
// is right for a Node but not for a Resource. A Variant holding a RefCounted owns
// a reference, so the pointer would dangle the moment the temporary died. Here
// the Ref takes its own reference first.
static Ref<SaveGame> instantiate_save_game(const Ref<Script>& script)
{
	Ref<GDScript> gd_script = script;
	if (gd_script.is_valid())
	{
		const Variant instance = gd_script->new_();
		return Ref<SaveGame>(instance);
	}

	// Any other scripting language: build the base type and attach the script.
	const StringName base_type = script->get_instance_base_type();
	if (base_type == StringName()) { return Ref<SaveGame>(); }

	const Variant instance = ClassDBSingleton::get_singleton()->instantiate(base_type);
	Ref<SaveGame> save_game = Ref<SaveGame>(instance);
	if (save_game.is_valid()) { save_game->set_script(script); }

	return save_game;
}

Ref<SaveGame> ProjectStatics::load_game(const String& slot_name, bool encrypt, const Ref<Script>& save_game_script)
{
	Ref<FileAccess> file;
	if (encrypt)
	{
		file = FileAccess::open_encrypted_with_pass(SAVES_LOCATION + slot_name + ".sav", FileAccess::ModeFlags::READ, resolve_encryption_key());
	}
	else
	{
		file = FileAccess::open(SAVES_LOCATION + slot_name + ".sav", FileAccess::ModeFlags::READ);
	}

	if (file.is_null())
	{
		print_error("Save file not found");
		return Ref<SaveGame>();
	}

	String json_string = file->get_as_text();
	file->close();

	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(json_string);
	if (err != OK)
	{
		print_error("Failed to parse save game data");
		return Ref<SaveGame>();
	}

	Ref<SaveGame> save_game;
	if (save_game_script.is_valid())
	{
		save_game = instantiate_save_game(save_game_script);
		if (save_game.is_null())
		{
			print_error(vformat("Save game script '%s' does not extend SaveGame", save_game_script->get_path()));
			return Ref<SaveGame>();
		}
	}
	else
	{
		save_game.instantiate();
	}

	err = save_game->from_json(json->get_data());
	if (err != OK)
	{
		print_error("Failed to deserialize save game data");
		return Ref<SaveGame>();
	}

	print_line("Game loaded successfully.");
	return save_game;
}

GameInstance* ProjectStatics::get_game_instance(World* world)
{
	ERR_FAIL_NULL_V(world, nullptr);
	return world->get_game_instance();
}

GameModeBase* ProjectStatics::get_game_mode(World* world)
{
	ERR_FAIL_NULL_V(world, nullptr);
	return world->get_game_mode();
}

GameStateBase* ProjectStatics::get_game_state(World* world)
{
	ERR_FAIL_NULL_V(world, nullptr);
	return world->get_game_state();
}

Level* ProjectStatics::get_level(World* world)
{
	ERR_FAIL_NULL_V(world, nullptr);
	return world->get_level();
}

void ProjectStatics::open_level(World* world, const String& resource_path)
{
	ERR_FAIL_NULL(world);
	world->open_level(resource_path);
}

PlayerController* ProjectStatics::get_first_player_controller(World* world)
{
	ERR_FAIL_NULL_V(world, nullptr);
	return world->get_first_player_controller();
}

PlayerController* ProjectStatics::get_player_controller(World* world, int index)
{
	ERR_FAIL_NULL_V(world, nullptr);
	return world->get_player_controller_at(index);
}

int ProjectStatics::get_player_controller_count(World* world)
{
	ERR_FAIL_NULL_V(world, 0);
	return world->get_player_controller_count();
}

LocalPlayer* ProjectStatics::get_local_player(World* world, int index)
{
	ERR_FAIL_NULL_V(world, nullptr);
	return world->get_local_player(index);
}

int ProjectStatics::get_local_player_count(World* world)
{
	ERR_FAIL_NULL_V(world, 0);
	return world->get_local_player_count();
}

void ProjectStatics::_bind_methods()
{
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("save_game", "slot_name", "data", "encrypt"), &ProjectStatics::save_game, DEFVAL(true));
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("load_game", "slot_name", "encrypt", "save_game_script"), &ProjectStatics::load_game, DEFVAL(true), DEFVAL(Ref<Script>()));
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_game_instance", "world"), &ProjectStatics::get_game_instance);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_game_mode", "world"), &ProjectStatics::get_game_mode);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_level", "world"), &ProjectStatics::get_level);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_game_state", "world"), &ProjectStatics::get_game_state);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("open_level", "world", "resource_path"), &ProjectStatics::open_level);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_first_player_controller", "world"), &ProjectStatics::get_first_player_controller);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_player_controller", "world", "index"), &ProjectStatics::get_player_controller);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_player_controller_count", "world"), &ProjectStatics::get_player_controller_count);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_local_player", "world", "index"), &ProjectStatics::get_local_player);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_local_player_count", "world"), &ProjectStatics::get_local_player_count);
}
}