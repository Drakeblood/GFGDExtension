#include "core/project_statics.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/templates/hash_map.hpp>

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

static String slot_path(const String& slot_name)
{
	return String(SAVES_LOCATION) + slot_name + ".sav";
}

// Both of these are function-local so their constructors run on first use.
// A namespace-scope godot::String container would be built during DLL load,
// before the GDExtension interface exists - the same reason the paths above are
// plain char pointers.
static HashMap<String, ProjectStatics::LoadResult>& slot_results()
{
	static HashMap<String, ProjectStatics::LoadResult> results;
	return results;
}

static ProjectStatics::LoadResult& last_result()
{
	static ProjectStatics::LoadResult result = ProjectStatics::LOAD_OK;
	return result;
}

static Ref<SaveGame> fail_load(const String& slot_name, ProjectStatics::LoadResult result)
{
	slot_results()[slot_name] = result;
	last_result() = result;
	return Ref<SaveGame>();
}

static const char* describe_load_result(ProjectStatics::LoadResult result)
{
	switch (result)
	{
		case ProjectStatics::LOAD_UNREADABLE:   return "could not be opened or decrypted (most often a changed save_encryption_key)";
		case ProjectStatics::LOAD_INVALID_DATA: return "was rejected by from_json()";
		case ProjectStatics::LOAD_BAD_SCRIPT:   return "was read with a script that does not extend SaveGame";
		default:                                return "could not be read";
	}
}

bool ProjectStatics::has_save_game(const String& slot_name)
{
	return FileAccess::file_exists(slot_path(slot_name));
}

ProjectStatics::LoadResult ProjectStatics::get_last_load_result()
{
	return last_result();
}

ProjectStatics::LoadResult ProjectStatics::get_slot_load_result(const String& slot_name)
{
	HashMap<String, LoadResult>::ConstIterator found = slot_results().find(slot_name);
	return found == slot_results().end() ? LOAD_OK : found->value;
}

bool ProjectStatics::is_slot_write_blocked(const String& slot_name)
{
	const LoadResult result = get_slot_load_result(slot_name);
	return result != LOAD_OK && result != LOAD_NOT_FOUND;
}

void ProjectStatics::clear_load_failure(const String& slot_name)
{
	slot_results().erase(slot_name);
}

Error ProjectStatics::save_game(const String& slot_name, const Ref<SaveGame>& data, bool encrypt)
{
	if (data.is_null()) return ERR_INVALID_PARAMETER;

	// The whole point of this guard: a slot that exists but would not load is a
	// player's save that only looks absent. Writing over it is the one mistake
	// this API cannot take back, and the shape that invites it - "load, and if
	// that gave me nothing, start fresh and save" - is the obvious one to write.
	if (is_slot_write_blocked(slot_name))
	{
		print_error(vformat("GFGD: refusing to write save slot '%s'. It %s when it was read this session, so the file that is there is being kept rather than replaced. Call ProjectStatics.clear_load_failure(\"%s\") to overwrite it deliberately.",
			slot_name, describe_load_result(get_slot_load_result(slot_name)), slot_name));
		return ERR_LOCKED;
	}

	Ref<DirAccess> dir = DirAccess::open(USER_LOCATION);
	if (dir.is_null()) return ERR_CANT_OPEN;

	if (!dir->dir_exists(SAVES_FOLDER))
	{
		Error err = dir->make_dir(SAVES_FOLDER);
		if (err != OK)
		{
			print_error("Failed to create saves directory");
			return err;
		}
	}

	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(data->to_json());
	if (err != OK)
	{
		print_error("Failed to serialize save game data");
		return ERR_INVALID_DATA;
	}

	String json_string = json->get_data().stringify();

	Ref<FileAccess> file;
	if (encrypt)
	{
		file = FileAccess::open_encrypted_with_pass(slot_path(slot_name), FileAccess::ModeFlags::WRITE, resolve_encryption_key());
	}
	else
	{
		file = FileAccess::open(slot_path(slot_name), FileAccess::ModeFlags::WRITE);
	}

	if (file.is_null())
	{
		print_error("Failed to open save file for writing");
		return ERR_FILE_CANT_WRITE;
	}

	file->store_string(json_string);
	file->close();
	print_line("Game saved successfully.");
	return OK;
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
	// Tested before opening, because every other failure below also produces a
	// null file handle. Without this, "the player has never played" and "the
	// player's save is right there and we cannot read it" are the same answer.
	if (!has_save_game(slot_name))
	{
		// Not an error: a first run reaches this every time.
		return fail_load(slot_name, LOAD_NOT_FOUND);
	}

	Ref<FileAccess> file;
	if (encrypt)
	{
		file = FileAccess::open_encrypted_with_pass(slot_path(slot_name), FileAccess::ModeFlags::READ, resolve_encryption_key());
	}
	else
	{
		file = FileAccess::open(slot_path(slot_name), FileAccess::ModeFlags::READ);
	}

	if (file.is_null())
	{
		print_error(vformat("GFGD: save slot '%s' exists but could not be opened. If application/game_framework/save_encryption_key changed, this is why. Saving to this slot is blocked until ProjectStatics.clear_load_failure(\"%s\") is called.", slot_name, slot_name));
		return fail_load(slot_name, LOAD_UNREADABLE);
	}

	String json_string = file->get_as_text();
	file->close();

	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(json_string);
	if (err != OK)
	{
		print_error(vformat("GFGD: save slot '%s' is not valid JSON. Saving to this slot is blocked until ProjectStatics.clear_load_failure(\"%s\") is called.", slot_name, slot_name));
		return fail_load(slot_name, LOAD_INVALID_DATA);
	}

	Ref<SaveGame> save_game;
	if (save_game_script.is_valid())
	{
		save_game = instantiate_save_game(save_game_script);
		if (save_game.is_null())
		{
			print_error(vformat("Save game script '%s' does not extend SaveGame", save_game_script->get_path()));
			return fail_load(slot_name, LOAD_BAD_SCRIPT);
		}
	}
	else
	{
		save_game.instantiate();
	}

	err = save_game->from_json(json->get_data());
	if (err != OK)
	{
		print_error(vformat("GFGD: save slot '%s' was rejected by from_json(). Saving to this slot is blocked until ProjectStatics.clear_load_failure(\"%s\") is called.", slot_name, slot_name));
		return fail_load(slot_name, LOAD_INVALID_DATA);
	}

	slot_results()[slot_name] = LOAD_OK;
	last_result() = LOAD_OK;
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
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("has_save_game", "slot_name"), &ProjectStatics::has_save_game);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_last_load_result"), &ProjectStatics::get_last_load_result);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_slot_load_result", "slot_name"), &ProjectStatics::get_slot_load_result);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("is_slot_write_blocked", "slot_name"), &ProjectStatics::is_slot_write_blocked);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("clear_load_failure", "slot_name"), &ProjectStatics::clear_load_failure);

	BIND_ENUM_CONSTANT(LOAD_OK);
	BIND_ENUM_CONSTANT(LOAD_NOT_FOUND);
	BIND_ENUM_CONSTANT(LOAD_UNREADABLE);
	BIND_ENUM_CONSTANT(LOAD_INVALID_DATA);
	BIND_ENUM_CONSTANT(LOAD_BAD_SCRIPT);
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