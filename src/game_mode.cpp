#include "game_mode.h"
#include <godot_cpp/core/class_db.hpp>

#include "gfgd_scene_tree.h"
#include "game_mode_settings.h"

using namespace godot;

namespace GFGD
{
GameMode::GameMode()
{
	
}

GameMode::~GameMode()
{
	
}

void GameMode::init_game(GFGDSceneTree* scene_tree)
{

}

void GameMode::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_game_mode_settings"), &GameMode::get_game_mode_settings);
    ClassDB::bind_method(D_METHOD("set_game_mode_settings", "script"), &GameMode::set_game_mode_settings);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_mode_settings", PROPERTY_HINT_RESOURCE_TYPE, "GameModeSettings"), "set_game_mode_settings", "get_game_mode_settings");
}
}