#include "game_mode_settings.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
void GameModeSettings::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_game_mode_script"), &GameModeSettings::get_game_mode_script);
    ClassDB::bind_method(D_METHOD("set_game_mode_script", "script"), &GameModeSettings::set_game_mode_script);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_mode_script", PROPERTY_HINT_RESOURCE_TYPE, "Script"), "set_game_mode_script", "get_game_mode_script");

    ClassDB::bind_method(D_METHOD("get_pawn_scene"), &GameModeSettings::get_pawn_scene);
    ClassDB::bind_method(D_METHOD("set_pawn_scene", "scene"), &GameModeSettings::set_pawn_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "pawn_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_pawn_scene", "get_pawn_scene");

    ClassDB::bind_method(D_METHOD("get_player_controller_scene"), &GameModeSettings::get_player_controller_scene);
    ClassDB::bind_method(D_METHOD("set_player_controller_scene", "scene"), &GameModeSettings::set_player_controller_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_controller_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_player_controller_scene", "get_player_controller_scene");
}
}