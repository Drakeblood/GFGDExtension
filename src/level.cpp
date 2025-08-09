#include "level.h"
#include <godot_cpp/core/class_db.hpp>

#include "gfgd_scene_tree.h"
#include "game_mode_settings.h"

using namespace godot;

namespace GFGD
{
Level::Level()
{
	
}

Level::~Level()
{
	
}

void Level::init_level(GFGDSceneTree* scene_tree)
{

}

void Level::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_game_mode_settings_override"), &Level::get_game_mode_settings_override);
    ClassDB::bind_method(D_METHOD("set_game_mode_settings_override", "script"), &Level::set_game_mode_settings_override);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_mode_settings_override", PROPERTY_HINT_RESOURCE_TYPE, "GameModeSettings"), "set_game_mode_settings_override", "get_game_mode_settings_override");
}
}