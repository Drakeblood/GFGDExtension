#include "framework/game_state.h"
#include <godot_cpp/core/class_db.hpp>

#include "framework/player_state.h"

using namespace godot;

namespace GFGD
{
GameState::GameState()
{
	scene_tree = nullptr;
}

GameState::~GameState()
{

}

void GameState::init_game_state(GFGDSceneTree* in_scene_tree)
{
	scene_tree = in_scene_tree;
	GDVIRTUAL_CALL(_init_game_state, in_scene_tree);
}

void GameState::add_player_state(PlayerState* player_state)
{
	if (player_state == nullptr) { return; }
	if (player_array.has(player_state)) { return; }

	if (player_state->get_parent() != this)
	{
		if (player_state->get_parent() != nullptr)
		{
			player_state->get_parent()->remove_child(player_state);
		}
		add_child(player_state);
	}

	player_array.append(player_state);
	emit_signal("player_state_added", player_state);
}

void GameState::remove_player_state(PlayerState* player_state)
{
	if (player_state == nullptr) { return; }

	const int index = player_array.find(player_state);
	if (index < 0) { return; }

	player_array.remove_at(index);
	emit_signal("player_state_removed", player_state);

	if (player_state->get_parent() == this)
	{
		remove_child(player_state);
		player_state->queue_free();
	}
}

PlayerState* GameState::get_player_state_by_index(int player_index) const
{
	for (int i = 0; i < player_array.size(); i++)
	{
		PlayerState* player_state = Object::cast_to<PlayerState>(player_array[i].get_validated_object());
		if (player_state != nullptr && player_state->get_player_index() == player_index)
		{
			return player_state;
		}
	}

	return nullptr;
}

PlayerState* GameState::get_player_state_by_unique_id(int unique_id) const
{
	for (int i = 0; i < player_array.size(); i++)
	{
		PlayerState* player_state = Object::cast_to<PlayerState>(player_array[i].get_validated_object());
		if (player_state != nullptr && player_state->get_unique_id() == unique_id)
		{
			return player_state;
		}
	}

	return nullptr;
}

void GameState::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("add_player_state", "player_state"), &GameState::add_player_state);
	ClassDB::bind_method(D_METHOD("remove_player_state", "player_state"), &GameState::remove_player_state);
	ClassDB::bind_method(D_METHOD("get_player_array"), &GameState::get_player_array);
	ClassDB::bind_method(D_METHOD("get_player_count"), &GameState::get_player_count);
	ClassDB::bind_method(D_METHOD("get_player_state_by_index", "player_index"), &GameState::get_player_state_by_index);
	ClassDB::bind_method(D_METHOD("get_player_state_by_unique_id", "unique_id"), &GameState::get_player_state_by_unique_id);
	ClassDB::bind_method(D_METHOD("get_gfgd_scene_tree"), &GameState::get_gfgd_scene_tree);

	GDVIRTUAL_BIND(_init_game_state, "scene_tree");

	ADD_SIGNAL(MethodInfo("player_state_added", PropertyInfo(Variant::OBJECT, "player_state", PROPERTY_HINT_NODE_TYPE, "PlayerState")));
	ADD_SIGNAL(MethodInfo("player_state_removed", PropertyInfo(Variant::OBJECT, "player_state", PROPERTY_HINT_NODE_TYPE, "PlayerState")));
}
}
