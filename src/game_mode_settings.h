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

public:
    Ref<Script> get_game_mode_script() const { return game_mode_script; }
    void set_game_mode_script(const Ref<Script> &script) { game_mode_script = script; }

    Ref<PackedScene> get_pawn_scene() const { return pawn_scene; }
    void set_pawn_scene(const Ref<PackedScene> &scene) { pawn_scene = scene; }

    Ref<PackedScene> get_player_controller_scene() const { return player_controller_scene; }
    void set_player_controller_scene(const Ref<PackedScene> &scene) { player_controller_scene = scene; }

protected:
	static void _bind_methods();

};
}

#endif