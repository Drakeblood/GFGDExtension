#include "framework/game_mode_settings.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
GameModeSettings::GameModeSettings()
{
    max_local_players = 1;
    allow_press_to_join = false;
}

GameModeSettings::~GameModeSettings()
{

}

Ref<PackedScene> GameModeSettings::get_pawn_scene_for_player(int player_index) const
{
    if (player_index >= 0 && player_index < pawn_scene_overrides.size())
    {
        Ref<PackedScene> override_scene = pawn_scene_overrides[player_index];
        if (override_scene.is_valid()) { return override_scene; }
    }

    return pawn_scene;
}

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

    ClassDB::bind_method(D_METHOD("get_game_state_scene"), &GameModeSettings::get_game_state_scene);
    ClassDB::bind_method(D_METHOD("set_game_state_scene", "scene"), &GameModeSettings::set_game_state_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_state_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_game_state_scene", "get_game_state_scene");

    ClassDB::bind_method(D_METHOD("get_player_state_scene"), &GameModeSettings::get_player_state_scene);
    ClassDB::bind_method(D_METHOD("set_player_state_scene", "scene"), &GameModeSettings::set_player_state_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_state_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_player_state_scene", "get_player_state_scene");

    ClassDB::bind_method(D_METHOD("get_max_local_players"), &GameModeSettings::get_max_local_players);
    ClassDB::bind_method(D_METHOD("set_max_local_players", "value"), &GameModeSettings::set_max_local_players);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_local_players", PROPERTY_HINT_RANGE, "1,8,1"), "set_max_local_players", "get_max_local_players");

    ClassDB::bind_method(D_METHOD("get_allow_press_to_join"), &GameModeSettings::get_allow_press_to_join);
    ClassDB::bind_method(D_METHOD("set_allow_press_to_join", "value"), &GameModeSettings::set_allow_press_to_join);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_press_to_join"), "set_allow_press_to_join", "get_allow_press_to_join");

    ClassDB::bind_method(D_METHOD("get_pawn_scene_overrides"), &GameModeSettings::get_pawn_scene_overrides);
    ClassDB::bind_method(D_METHOD("set_pawn_scene_overrides", "value"), &GameModeSettings::set_pawn_scene_overrides);
    ClassDB::bind_method(D_METHOD("get_pawn_scene_for_player", "player_index"), &GameModeSettings::get_pawn_scene_for_player);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "pawn_scene_overrides", PROPERTY_HINT_TYPE_STRING,
            vformat("%d/%d:PackedScene", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_pawn_scene_overrides", "get_pawn_scene_overrides");
}
}
