#ifndef GAME_MODE_BASE_H
#define GAME_MODE_BASE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/typed_array.hpp>

#include "framework/world.h"

// A virtual's argument types have to be complete, so these cannot be forward
// declarations.
#include "framework/controller.h"
#include "framework/player_controller.h"

using namespace godot;

namespace GFGD
{
class LocalPlayer;
class PlayerState;

// The rules of the level, and the scenes everything else is built from.
//
// It exists on the server only: it is what decides who may join, where they
// spawn and when the match is over. The list of players everyone is allowed to
// see lives on the GameState instead.
//
// The scenes below are read on a client too, without the game mode ever being
// part of its world - both sides have to build the same things for a player,
// and this is where that choice is written down.
class GameModeBase : public Node
{
	GDCLASS(GameModeBase, Node)

private:
	Ref<PackedScene> game_state_scene;
	Ref<PackedScene> player_state_scene;
	Ref<PackedScene> player_controller_scene;
	Ref<PackedScene> default_pawn_scene;

	// Indexed by local player index; falls back to default_pawn_scene when short
	// or null.
	TypedArray<PackedScene> pawn_scene_overrides;

	// How many humans may share this machine. Leaving it at 1 keeps player 0
	// accepting every device, as the global input singleton always did.
	int max_local_players;

	// When true, a button on a device no player owns logs a new local player in,
	// up to max_local_players. Gate it further with _can_join.
	bool allow_press_to_join;

	// How many players the session accepts, counting everyone on every machine.
	int max_players;

	World* world;

	// Start nodes already handed out this level, so two players do not spawn
	// inside each other.
	Array claimed_player_starts;

public:
	GameModeBase();
	~GameModeBase();

	void init_game(World* world);

	// Marks the match as begun and lets everyone know.
	void start_play();

	// Creates the controller, the player state and a pawn for one human at this
	// machine.
	PlayerController* login_local_player(LocalPlayer* local_player);

	// The same for a player at another machine, once that machine has reported
	// the level up.
	PlayerController* login_peer(int peer_id);

	void logout(PlayerController* player_controller);
	void logout_peer(int peer_id);

	// Logs in every local player that does not have a controller yet, up to
	// max_local_players.
	void restart_all_players();

	// Spawns a fresh pawn at a chosen start and possesses it.
	void restart_player(Controller* controller);
	void restart_player_at(Controller* controller, Node* start_spot);

	Node* choose_player_start(Controller* controller);
	Node* find_player_start(const StringName& player_start_tag) const;
	void release_player_start(Controller* controller);

	// Called by the InputRouter when a device nobody owns is pressed. Returns true
	// if a local player was logged in.
	bool try_join(int device_slot);

	int get_num_players() const;
	Array get_player_controllers() const;
	PlayerController* get_player_controller_at(int index) const;

	World* get_world() const { return world; }

	// Ensures local player 0 exists, logs it in and returns its controller.
	PlayerController* spawn_default_player();

	Ref<PackedScene> get_pawn_scene_for_player(int player_index) const;
	Ref<PackedScene> get_pawn_scene_for(Controller* controller);

	Ref<PackedScene> get_game_state_scene() const { return game_state_scene; }
	void set_game_state_scene(const Ref<PackedScene>& value) { game_state_scene = value; }

	Ref<PackedScene> get_player_state_scene() const { return player_state_scene; }
	void set_player_state_scene(const Ref<PackedScene>& value) { player_state_scene = value; }

	Ref<PackedScene> get_player_controller_scene() const { return player_controller_scene; }
	void set_player_controller_scene(const Ref<PackedScene>& value) { player_controller_scene = value; }

	Ref<PackedScene> get_default_pawn_scene() const { return default_pawn_scene; }
	void set_default_pawn_scene(const Ref<PackedScene>& value) { default_pawn_scene = value; }

	TypedArray<PackedScene> get_pawn_scene_overrides() const { return pawn_scene_overrides; }
	void set_pawn_scene_overrides(const TypedArray<PackedScene>& value) { pawn_scene_overrides = value; }

	int get_max_local_players() const { return max_local_players; }
	void set_max_local_players(int value) { max_local_players = value; }

	bool get_allow_press_to_join() const { return allow_press_to_join; }
	void set_allow_press_to_join(bool value) { allow_press_to_join = value; }

	int get_max_players() const { return max_players; }
	void set_max_players(int value) { max_players = value; }

	// Return true from a script override to signal that player spawning was
	// handled manually.
	GDVIRTUAL1R(bool, _init_game, World*)

	// Runs after the game state exists and before any player is logged in.
	GDVIRTUAL0(_init_game_state)

	// Return an error message to refuse a connecting peer, or an empty string to
	// let it in.
	GDVIRTUAL1R(String, _pre_login, int)

	// Return a start node to place the pawn at, or null to use the default search.
	GDVIRTUAL1R(Node*, _choose_player_start, Controller*)

	// Return the scene to build this controller's pawn from. Returning a scene
	// rather than a spawned node is what lets every peer build the same pawn.
	GDVIRTUAL1R(Ref<PackedScene>, _get_pawn_scene_for, Controller*)

	// Return false to refuse a press-to-join from this device slot. This is where
	// "two pads only, ignore the keyboard" belongs.
	GDVIRTUAL1R(bool, _can_join, int)

	GDVIRTUAL1(_on_post_login, PlayerController*)
	GDVIRTUAL1(_on_logout, PlayerController*)

protected:
	static void _bind_methods();

private:
	PlayerController* login_internal(int peer_id, LocalPlayer* local_player);
	Variant start_transform_for(Node* start_spot) const;
	void refuse_peer(int peer_id, const String& reason);
};
}

#endif
