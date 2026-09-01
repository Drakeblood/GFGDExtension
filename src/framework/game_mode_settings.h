#ifndef GAME_MODE_SETTINGS_H
#define GAME_MODE_SETTINGS_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/packed_scene.hpp>

using namespace godot;

namespace GFGD
{
class GameModeSettings : public Resource
{
	GDCLASS(GameModeSettings, Resource)

private:
	Ref<Script> game_mode_script;
	Ref<PackedScene> pawn_scene;
    Ref<PackedScene> player_controller_scene;
    Ref<PackedScene> game_state_scene;
    Ref<PackedScene> player_state_scene;

    // How many humans may share this machine. Leaving it at 1 keeps the old
    // behaviour exactly: player 0 accepts every device, as the global Input
    // singleton always did.
    int max_local_players;

    // When true, a button on a device no player owns logs a new player in, up to
    // max_local_players. Gate it further with GameMode._can_join.
    bool allow_press_to_join;

    // Indexed by player index; falls back to pawn_scene when short or null.
    TypedArray<PackedScene> pawn_scene_overrides;

public:
    GameModeSettings();
    ~GameModeSettings();

    Ref<Script> get_game_mode_script() const { return game_mode_script; }
    void set_game_mode_script(const Ref<Script> &script) { game_mode_script = script; }

    Ref<PackedScene> get_pawn_scene() const { return pawn_scene; }
    void set_pawn_scene(const Ref<PackedScene> &scene) { pawn_scene = scene; }

    Ref<PackedScene> get_player_controller_scene() const { return player_controller_scene; }
    void set_player_controller_scene(const Ref<PackedScene> &scene) { player_controller_scene = scene; }

    Ref<PackedScene> get_game_state_scene() const { return game_state_scene; }
    void set_game_state_scene(const Ref<PackedScene> &scene) { game_state_scene = scene; }

    Ref<PackedScene> get_player_state_scene() const { return player_state_scene; }
    void set_player_state_scene(const Ref<PackedScene> &scene) { player_state_scene = scene; }

    int get_max_local_players() const { return max_local_players; }
    void set_max_local_players(int value) { max_local_players = value; }

    bool get_allow_press_to_join() const { return allow_press_to_join; }
    void set_allow_press_to_join(bool value) { allow_press_to_join = value; }

    TypedArray<PackedScene> get_pawn_scene_overrides() const { return pawn_scene_overrides; }
    void set_pawn_scene_overrides(const TypedArray<PackedScene> &value) { pawn_scene_overrides = value; }

    Ref<PackedScene> get_pawn_scene_for_player(int player_index) const;

protected:
	static void _bind_methods();

};
}

#endif
