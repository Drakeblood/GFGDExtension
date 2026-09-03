#ifndef WORLD_H
#define WORLD_H

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/binder_common.hpp>

using namespace godot;

namespace GFGD
{

class Controller;
class GameInstance;
class GameModeBase;
class GameStateBase;
class InputRouter;
class Level;
class LocalPlayer;
class NetDriver;
class PlayerController;

// The world. It owns what belongs to the level - the game mode, the game state
// and the list of controllers - while the GameInstance it holds owns what
// outlives a level, which is the local players.
class World : public SceneTree
{
	GDCLASS(World, SceneTree)

public:
	// How this process takes part in the session. Standalone is a game with no
	// peer at all; a listen server is a server somebody is playing on.
	enum NetMode
	{
		NET_MODE_STANDALONE = 0,
		NET_MODE_DEDICATED_SERVER = 1,
		NET_MODE_LISTEN_SERVER = 2,
		NET_MODE_CLIENT = 3,
	};

	// What this process may do with a given node. Authority decides, an
	// autonomous proxy is the copy belonging to the human driving it, and a
	// simulated proxy is somebody else's copy that only receives updates.
	enum NetRole
	{
		ROLE_NONE = 0,
		ROLE_SIMULATED_PROXY = 1,
		ROLE_AUTONOMOUS_PROXY = 2,
		ROLE_AUTHORITY = 3,
	};

	// The peer id a server always has, and the id a game with no peer reports.
	static constexpr int SERVER_PEER_ID = 1;

private:
	GameInstance* game_instance;
	GameModeBase* game_mode;

	// The same object as game_mode on a server. On a client it is an instance
	// that is deliberately never added to the tree: the game mode's rules are the
	// server's business, but its default scenes decide what the spawners build,
	// and both sides have to agree on those.
	GameModeBase* game_mode_defaults;

	GameStateBase* game_state;
	Level* level;

	// Created once and never freed with a level, so device assignment never has
	// a gap across a level change.
	InputRouter* input_router;

	// Connection handling, travel and the spawners. Lives at a fixed path on
	// every peer, because that is what remote calls address each other by.
	NetDriver* net_driver;

	// Fixed parents, so a controller and a pawn are found under the same path on
	// every peer no matter what the level scene is called.
	Node* players_container;
	Node* pawn_container;

	// Controllers put themselves in and take themselves out from _enter_tree and
	// _exit_tree, so the lists cover controllers the game mode never created and
	// can never hold a freed one.
	Array controller_list;
	Array player_controller_list;

	// Handed out by the server and never reused within a session, so a name
	// derived from it identifies the same player on every peer.
	int next_player_id;

	bool dedicated_server;

public:
	World();
	~World();

	virtual void _initialize() override;
	virtual void _finalize() override;

	Level* find_level();

	void set_game_instance(GameInstance* instance) { game_instance = instance; }
	GameInstance* get_game_instance() const { return game_instance; }

	// Null on a client: the rules are the server's, and asking for them is a
	// mistake worth seeing rather than a silent empty object.
	GameModeBase* get_game_mode() const { return game_mode; }

	// The default scenes the game mode declares, readable on every peer.
	GameModeBase* get_game_mode_defaults() const { return game_mode_defaults; }

	GameStateBase* get_game_state() const { return game_state; }
	Level* get_level() const { return level; }
	InputRouter* get_input_router() const { return input_router; }

	// The router hands back a null here when it leaves the tree, so the world
	// never holds a pointer to a node the root has already torn down.
	void set_input_router(InputRouter* value) { input_router = value; }
	NetDriver* get_net_driver() const { return net_driver; }
	Node* get_players_container() const { return players_container; }
	Node* get_pawn_container() const { return pawn_container; }

	int take_next_player_id() { return next_player_id++; }

	// Standalone level change. On a server this forwards to server_travel; a
	// client cannot change level on its own and is told so.
	void open_level(const String& resource_path);

	// Idempotent, so a game mode may register a controller eagerly before it
	// enters the tree without producing a duplicate.
	void register_controller(Controller* controller);
	void unregister_controller(Controller* controller);

	Array get_controllers() const;
	Array get_player_controllers() const;

	// The world owns the player list, so this is where "who is player one" is
	// answered - the game mode does not exist on a client to answer it.
	PlayerController* get_first_player_controller() const;
	PlayerController* get_player_controller_at(int index) const;
	int get_player_controller_count() const;
	PlayerController* find_player_controller_by_player_id(int player_id) const;

	// Convenience wrappers over the GameInstance, so scripts have one obvious
	// place to reach local players from.
	LocalPlayer* create_local_player(int device_slot);
	LocalPlayer* create_default_local_player();
	LocalPlayer* get_local_player(int index) const;
	int get_local_player_count() const;

	// --- Session -----------------------------------------------------------

	// Hosts on this machine and keeps playing on it.
	Error host_game(int port, int max_players = 32);

	// Hosts without a player of its own: no local player, no input routing and
	// no camera work.
	Error create_dedicated_server(int port, int max_players = 32);

	// Connects. The level to load arrives from the server, so nothing changes
	// locally until it does.
	Error join_game(const String& address, int port);

	void disconnect_from_network();

	NetMode get_net_mode() const;
	bool is_dedicated_server() const;
	bool is_listen_server() const;
	bool is_client() const;
	bool is_networked() const;

	// True with no peer at all, which is what makes every rule written for a
	// server also correct in a standalone game.
	bool has_authority() const;
	int get_local_peer_id() const;

	// Loads on the server and takes every connected client along.
	void server_travel(const String& resource_path);

	// Tears the current level down and builds the given one here only. The
	// travel paths use it once they have agreed on what to load.
	void load_level(const String& resource_path);

	// --- Ownership and roles ------------------------------------------------

	// Walks up the ownership chain - a pawn is owned by its controller, and a
	// player controller by the peer it was created for.
	int get_net_owner_peer(Node* node) const;

	NetRole get_local_role_for(Node* node) const;
	NetRole get_remote_role_for(Node* node) const;

protected:
	static void _bind_methods();

private:
	void create_game_instance();
	void create_net_driver();
	void create_input_router();
	void create_containers();
	void create_game_state();
	void resolve_game_mode(const Ref<PackedScene>& game_mode_scene);
	Ref<PackedScene> resolve_game_mode_scene() const;
	void start_level();
	void initialize_game();
	void destroy_level_nodes();
	void prune_controller_lists();
};
}

VARIANT_ENUM_CAST(GFGD::World::NetMode);
VARIANT_ENUM_CAST(GFGD::World::NetRole);

#endif
