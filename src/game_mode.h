#ifndef GAME_MODE_H
#define GAME_MODE_H

#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace GFGD 
{
class GFGDSceneTree;
class GameModeSettings;

class GameMode : public Node
{
	GDCLASS(GameMode, Node)

private:
	Ref<GameModeSettings> game_mode_settings;

public:
	GameMode();
	~GameMode();

	virtual void init_game(GFGDSceneTree* scene_tree);

    Ref<GameModeSettings> get_game_mode_settings() const { return game_mode_settings; }
    void set_game_mode_settings(const Ref<GameModeSettings> &script) { game_mode_settings = script; }

protected:
	static void _bind_methods();

};
}

#endif