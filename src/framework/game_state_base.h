#ifndef GAME_STATE_BASE_H
#define GAME_STATE_BASE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "framework/world.h"

using namespace godot;

namespace GFGD
{
class PlayerState;

// What everyone is allowed to know about the match. Created by the World rather
// than by the game mode, because the game mode is the server's alone while this
// has to exist on clients too - that split is the whole reason the class is
// separate.
class GameStateBase : public Node
{
	GDCLASS(GameStateBase, Node)

private:
	World* world;
	Array player_array;

	// Set once the game mode has started the level. Clients read it to know the
	// match is really running rather than still being built.
	bool has_begun_play;

	// The server's clock, so every peer can agree on when something happened.
	double server_world_time;

	// Local reading of the clock when server_world_time last arrived, which is
	// what lets a client carry it forward between updates.
	double server_world_time_received_at;
	double last_seen_server_world_time;

	float server_world_time_update_interval;
	float time_since_time_update;
	float time_since_ping_update;

public:
	GameStateBase();
	~GameStateBase();

	virtual void _ready() override;
	virtual void _process(double delta) override;

	void init_game_state(World* world);

	// Called by the game mode once the level is up and players may join.
	void handle_begin_play();

	// Reparents the PlayerState under this node and adds it to the list.
	void add_player_state(PlayerState* player_state);
	void remove_player_state(PlayerState* player_state);

	Array get_player_array() const { return player_array; }
	int get_player_count() const { return player_array.size(); }
	PlayerState* get_player_state_by_player_id(int player_id) const;
	PlayerState* get_player_state_by_index(int player_index) const;
	PlayerState* get_player_state_by_unique_id(int unique_id) const;

	bool get_has_begun_play() const { return has_begun_play; }
	void set_has_begun_play(bool value) { has_begun_play = value; }

	double get_server_world_time() const { return server_world_time; }
	void set_server_world_time(double value) { server_world_time = value; }

	// The server's clock as this peer best knows it: measured here on the server,
	// carried forward from the last update on a client.
	double get_server_world_time_seconds() const;

	World* get_world() const { return world; }
	bool has_authority() const;

	GDVIRTUAL1(_init_game_state, World*)

	// Names of this node's own properties to keep in step with the server's copy.
	GDVIRTUAL0R(PackedStringArray, _get_replicated_properties)

protected:
	static void _bind_methods();

private:
	void update_server_world_time(double delta);
	void update_pings();
};
}

#endif
