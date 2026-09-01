#ifndef LEVEL_H
#define LEVEL_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "framework/gfgd_scene_tree.h"
#include "framework/game_mode_settings.h"

using namespace godot;

namespace GFGD
{

class Level : public Node
{
	GDCLASS(Level, Node)

private:
	Ref<GameModeSettings> game_mode_settings_override;

public:
	Level();
	~Level();

	void init_level(GFGDSceneTree* scene_tree);

	Ref<GameModeSettings> get_game_mode_settings_override() const { return game_mode_settings_override; }
	void set_game_mode_settings_override(const Ref<GameModeSettings>& settings) { game_mode_settings_override = settings; }

	GDVIRTUAL1(_init_level, GFGDSceneTree*)

protected:
	static void _bind_methods();

};
}

#endif
