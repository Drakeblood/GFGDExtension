#ifndef LEVEL_H
#define LEVEL_H

#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace GFGD 
{
class GFGDSceneTree;
class GameModeSettings;

class Level : public Node
{
	GDCLASS(Level, Node)

private:
	Ref<GameModeSettings> game_mode_settings_override;

public:
	Level();
	~Level();

	virtual void init_level(GFGDSceneTree* scene_tree);

	Ref<GameModeSettings> get_game_mode_settings_override() const { return game_mode_settings_override; }
    void set_game_mode_settings_override(const Ref<GameModeSettings> &script) { game_mode_settings_override = script; }

protected:
	static void _bind_methods();

};
}

#endif