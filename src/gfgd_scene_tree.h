#ifndef GF_SCENE_TREE_H
#define GF_SCENE_TREE_H

#include <godot_cpp/classes/scene_tree.hpp>

using namespace godot;

namespace GFGD 
{

class GameInstance;
class GameMode;
class Level;

class GFGDSceneTree : public SceneTree
{
	GDCLASS(GFGDSceneTree, SceneTree)

private:
	GameInstance* game_instance;
	GameMode* game_mode;
	Level* level;

public:
	GFGDSceneTree();
	~GFGDSceneTree();

	virtual void _initialize() override;

	Level* find_level();

	void set_game_instance(GameInstance* instance) { game_instance = instance; }
    GameInstance* get_game_instance() const { return game_instance; }

    void set_game_mode(GameMode* mode) { game_mode = mode; }
    GameMode* get_game_mode() const { return game_mode; }

    void set_level(Level* lvl) { level = lvl; }
    Level* get_level() const { return level; }

    void open_level(const String& resource_path);

protected:
	static void _bind_methods();

private:
	void create_game_mode();

};
}

#endif