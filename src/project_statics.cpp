#include "project_statics.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>

#include "gfgd_scene_tree.h"
#include "game_instance.h"
#include "game_mode.h"
#include "level.h"
#include "save_game.h"

using namespace godot;

namespace GFGD
{
const String ProjectStatics::USER_LOCATION = "user://";
const String ProjectStatics::SAVES_FOLDER = "saves/";
const String ProjectStatics::SAVES_LOCATION = USER_LOCATION + SAVES_FOLDER;
const String ProjectStatics::SAVE_GAME_ENCRYPTION_KEY = "super_secret_password";

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

void ProjectStatics::_bind_methods()
{
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("save_game", "slot_name", "data", "encrypt"), &ProjectStatics::save_game);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("load_game", "slot_name", "encrypt"), &ProjectStatics::load_game);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_game_instance", "scene_tree"), &ProjectStatics::get_game_instance);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_game_mode", "scene_tree"), &ProjectStatics::get_game_mode);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("get_level", "scene_tree"), &ProjectStatics::get_level);
	ClassDB::bind_static_method("ProjectStatics", D_METHOD("open_level", "scene_tree", "resource_path"), &ProjectStatics::open_level);
}
}