#ifndef GAME_MODE_H
#define GAME_MODE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "framework/gfgd_scene_tree.h"
#include "framework/game_mode_settings.h"

// GDVIRTUAL needs the complete type of every argument, so these cannot be
// forward declarations.
#include "framework/controller.h"
#include "framework/player_controller.h"

using namespace godot;

namespace GFGD
{
class LocalPlayer;
class PlayerState;

// The rules of the level. Conceptually server-only: it is what decides who may
// join and where they spawn. The list of players everyone is allowed to see
// lives on the GameState instead.
class GameMode : public Node
{
	GDCLASS(GameMode, Node)

private:
	Ref<GameModeSettings> game_mode_settings;
	GFGDSceneTree* scene_tree;

	// Start nodes already handed out this level, so two players do not spawn
	// inside each other.
	Array claimed_player_starts;

public:
	GameMode();
	~GameMode();

	void init_game(GFGDSceneTree* scene_tree);

	// Creates the PlayerController and PlayerState for a local player and puts a
	// pawn under it.
	PlayerController* login(LocalPlayer* local_player);
	void logout(PlayerController* player_controller);

	// Logs in every local player that does not have a controller yet, up to
	// max_local_players. With the default single local player this does exactly
	// what spawn_default_player() always did.
	void restart_all_players();

	// Spawns a fresh pawn at a chosen start and possesses it.
	void restart_player(Controller* controller);
	Node* spawn_default_pawn_for(Controller* controller);
	Node* choose_player_start(Controller* controller);
	void release_player_start(Controller* controller);

	// Called by the InputRouter when a device nobody owns is pressed. Returns true
	// if a player was logged in.
	bool try_join(int device_slot);

	Array get_player_controllers() const;
	PlayerController* get_player_controller_at(int index) const;

	GFGDSceneTree* get_gfgd_scene_tree() const { return scene_tree; }

	// Ensures local player 0 exists, logs it in and returns its controller.
	PlayerController* spawn_default_player();

	Ref<GameModeSettings> get_game_mode_settings() const { return game_mode_settings; }
	void set_game_mode_settings(const Ref<GameModeSettings>& settings) { game_mode_settings = settings; }

	// Return true from a script override to signal that player spawning was handled manually.
	GDVIRTUAL1R(bool, _init_game, GFGDSceneTree*)

	// Return a start node to place the pawn at, or null to use the default search.
	GDVIRTUAL1R(Node*, _choose_player_start, Controller*)

	// Return a spawned pawn to take over pawn creation entirely; null uses the default.
	GDVIRTUAL1R(Node*, _spawn_default_pawn_for, Controller*)

	// Return false to refuse a press-to-join from this device slot. This is where
	// "two pads only, ignore the keyboard" belongs.
	GDVIRTUAL1R(bool, _can_join, int)

	GDVIRTUAL1(_on_post_login, PlayerController*)
	GDVIRTUAL1(_on_logout, PlayerController*)

protected:
	static void _bind_methods();

private:
	PlayerController* create_player_controller();
	PlayerState* create_player_state_for(PlayerController* player_controller);
	Node* get_pawn_parent() const;
};
}

#endif
