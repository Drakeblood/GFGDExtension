#include "core/project_statics.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>

#include "framework/gfgd_scene_tree.h"
#include "framework/game_instance.h"
#include "framework/game_mode.h"
#include "framework/game_state.h"
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
		file = FileAccess::open_encrypted_with_pass(SAVES_LOCATION + slot_name + ".sav", FileAccess::ModeFlags::WRITE, SAVE_GAME_ENCRYPTION_KEY);
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

Ref<SaveGame> ProjectStatics::load_game(const String& slot_name, bool encrypt)
{
	Ref<FileAccess> file;
	if (encrypt)
	{
		file = FileAccess::open_encrypted_with_pass(SAVES_LOCATION + slot_name + ".sav", FileAccess::ModeFlags::READ, SAVE_GAME_ENCRYPTION_KEY);
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
	save_game.instantiate();
	err = save_game->from_json(json->get_data());
	if (err != OK)
	{
		print_error("Failed to deserialize save game data");
		return Ref<SaveGame>();
	}

	print_line("Game loaded successfully.");
	return save_game;
}

GameInstance* ProjectStatics::get_game_instance(GFGDSceneTree* scene_tree)
{
	ERR_FAIL_NULL_V(scene_tree, nullptr);
	return scene_tree->get_game_instance();
}

GameMode* ProjectStatics::get_game_mode(GFGDSceneTree* scene_tree)
{
	ERR_FAIL_NULL_V(scene_tree, nullptr);
	return scene_tree->get_game_mode();
}

GameState* ProjectStatics::get_game_state(GFGDSceneTree* scene_tree)
{
	ERR_FAIL_NULL_V(scene_tree, nullptr);
	return scene_tree->get_game_state();
}

Level* ProjectStatics::get_level(GFGDSceneTree* scene_tree)
{
	ERR_FAIL_NULL_V(scene_tree, nullptr);
	return scene_tree->get_level();
}

void ProjectStatics::open_level(GFGDSceneTree* scene_tree, const String& resource_path)
{
	ERR_FAIL_NULL(scene_tree);
	scene_tree->open_level(resource_path);
}

PlayerController* ProjectStatics::get_first_player_controller(GFGDSceneTree* scene_tree)
{
	ERR_FAIL_NULL_V(scene_tree, nullptr);
	return scene_tree->get_first_player_controller();
}

PlayerController* ProjectStatics::get_player_controller(GFGDSceneTree* scene_tree, int index)
{
	ERR_FAIL_NULL_V(scene_tree, nullptr);
	return scene_tree->get_player_controller_at(index);
}

int ProjectStatics::get_player_controller_count(GFGDSceneTree* scene_tree)
{
	ERR_FAIL_NULL_V(scene_tree, 0);
	return scene_tree->get_player_controller_count();
}

LocalPlayer* ProjectStatics::get_local_player(GFGDSceneTree* scene_tree, int index)
{
	ERR_FAIL_NULL_V(scene_tree, nullptr);
	return scene_tree->get_local_player(index);
}

int ProjectStatics::get_local_player_count(GFGDSceneTree* scene_tree)
{
	ERR_FAIL_NULL_V(scene_tree, 0);
	return scene_tree->get_local_player_count();
}

void ProjectStatics::_bind_methods()
{
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("save_game", "slot_name", "data", "encrypt"), &ProjectStatics::save_game);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("load_game", "slot_name", "encrypt"), &ProjectStatics::load_game);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_game_instance", "scene_tree"), &ProjectStatics::get_game_instance);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_game_mode", "scene_tree"), &ProjectStatics::get_game_mode);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_level", "scene_tree"), &ProjectStatics::get_level);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_game_state", "scene_tree"), &ProjectStatics::get_game_state);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("open_level", "scene_tree", "resource_path"), &ProjectStatics::open_level);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_first_player_controller", "scene_tree"), &ProjectStatics::get_first_player_controller);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_player_controller", "scene_tree", "index"), &ProjectStatics::get_player_controller);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_player_controller_count", "scene_tree"), &ProjectStatics::get_player_controller_count);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_local_player", "scene_tree", "index"), &ProjectStatics::get_local_player);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_local_player_count", "scene_tree"), &ProjectStatics::get_local_player_count);
}
}