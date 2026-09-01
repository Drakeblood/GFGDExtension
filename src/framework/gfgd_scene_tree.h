#ifndef GF_SCENE_TREE_H
#define GF_SCENE_TREE_H

#include <godot_cpp/classes/scene_tree.hpp>

using namespace godot;

namespace GFGD
{

class Controller;
class GameInstance;
class GameMode;
class GameState;
class InputRouter;
class Level;
class LocalPlayer;
class PlayerController;

// The world. It owns what belongs to the level - the game mode, the game state
// and the list of controllers - while the GameInstance it holds owns what
// outlives a level, which is the local players.
class GFGDSceneTree : public SceneTree
{
	GDCLASS(GFGDSceneTree, SceneTree)

private:
	GameInstance* game_instance;
	GameMode* game_mode;
	GameState* game_state;
	Level* level;

	// Created once and never freed with a level, so device assignment never has
	// a gap across open_level.
	InputRouter* input_router;

	// Controllers put themselves in and take themselves out from _enter_tree and
	// _exit_tree, so the lists cover controllers the game mode never created and
	// can never hold a freed one.
	Array controller_list;
	Array player_controller_list;

public:
	GFGDSceneTree();
	~GFGDSceneTree();

	virtual void _initialize() override;
	virtual void _finalize() override;

	Level* find_level();

	void set_game_instance(GameInstance* instance) { game_instance = instance; }
	GameInstance* get_game_instance() const { return game_instance; }

	void set_game_mode(GameMode* mode) { game_mode = mode; }
	GameMode* get_game_mode() const { return game_mode; }

	void set_game_state(GameState* state) { game_state = state; }
	GameState* get_game_state() const { return game_state; }

	void set_level(Level* lvl) { level = lvl; }
	Level* get_level() const { return level; }

	void set_input_router(InputRouter* router) { input_router = router; }
	InputRouter* get_input_router() const { return input_router; }

	void open_level(const String& resource_path);

	// Idempotent, so a game mode may register a controller eagerly before it
	// enters the tree without producing a duplicate.
	void register_controller(Controller* controller);
	void unregister_controller(Controller* controller);

	Array get_controllers() const;
	Array get_player_controllers() const;

	// The world owns the player list, so this is where "who is player one" is
	// answered - the GameMode does not exist on a client to answer it.
	PlayerController* get_first_player_controller() const;
	PlayerController* get_player_controller_at(int index) const;
	int get_player_controller_count() const;

	// Convenience wrappers over the GameInstance, so scripts have one obvious
	// place to reach local players from.
	LocalPlayer* create_local_player(int device_slot);
	LocalPlayer* create_default_local_player();
	LocalPlayer* get_local_player(int index) const;
	int get_local_player_count() const;

	// Always true while there is no multiplayer peer. The hook exists so
	// server-only code can be written now and stay correct once there is one.
	bool has_authority() const;

protected:
	static void _bind_methods();

private:
	void create_game_instance();
	void create_input_router();
	void create_game_state();
	void create_game_mode();
	void initialize_game();
	void destroy_level_nodes();
	void prune_controller_lists();

};
}

#endif
