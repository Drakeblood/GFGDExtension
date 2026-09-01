#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "framework/gfgd_scene_tree.h"

using namespace godot;

namespace GFGD
{
class PlayerState;

// What everyone is allowed to know about the match. Created by GFGDSceneTree
// rather than by the GameMode, because the GameMode is server-only in principle
// while this has to exist on clients too - that split is the whole reason the
// class is separate.
class GameState : public Node
{
	GDCLASS(GameState, Node)

private:
	GFGDSceneTree* scene_tree;
	Array player_array;

public:
	GameState();
	~GameState();

	void init_game_state(GFGDSceneTree* scene_tree);

	// Reparents the PlayerState under this node and adds it to the list.
	void add_player_state(PlayerState* player_state);
	void remove_player_state(PlayerState* player_state);

	Array get_player_array() const { return player_array; }
	int get_player_count() const { return player_array.size(); }
	PlayerState* get_player_state_by_index(int player_index) const;
	PlayerState* get_player_state_by_unique_id(int unique_id) const;

	GFGDSceneTree* get_gfgd_scene_tree() const { return scene_tree; }

	GDVIRTUAL1(_init_game_state, GFGDSceneTree*)

protected:
	static void _bind_methods();
};
}

#endif
