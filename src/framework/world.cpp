#include "framework/world.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/offline_multiplayer_peer.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/window.hpp>

#include "framework/controller.h"
#include "framework/game_instance.h"
#include "framework/game_mode_base.h"
#include "framework/game_state_base.h"
#include "framework/input_router.h"
#include "framework/level.h"
#include "framework/local_player.h"
#include "framework/net_driver.h"
#include "framework/pawn.h"
#include "framework/player_controller.h"
#include "framework/player_input.h"
#include "framework/player_state.h"
#include "gameplay_tags/gameplay_tags_manager.h"
#include "core/gfgd_statics.h"

using namespace godot;

namespace GFGD
{
World::World()
{
	game_instance = nullptr;
	game_mode = nullptr;
	game_mode_defaults = nullptr;
	game_state = nullptr;
	level = nullptr;
	input_router = nullptr;
	net_driver = nullptr;
	players_container = nullptr;
	pawn_container = nullptr;
	next_player_id = 1;
	dedicated_server = false;
}

World::~World()
{

}

void World::_initialize()
{
	GameplayTagsManager::get_singleton();

	// An export template built for a server has no player of its own, so the
	// decision is already made before any level runs.
	dedicated_server = OS::get_singleton()->has_feature("dedicated_server");

	create_game_instance();
	game_instance->init(this);

	// The root only enters the tree after MainLoop::initialize() returns, so
	// defer level discovery and game mode creation to the first frame, when
	// the main scene is inside the tree and its _ready callbacks have run.
	connect("process_frame", callable_mp(this, &World::initialize_game), CONNECT_ONE_SHOT);
}

void World::initialize_game()
{
	create_net_driver();
	create_containers();

	if (!is_dedicated_server())
	{
		create_input_router();
	}

	// The main scene is the level, and Godot has already added it by now.
	level = find_level();

	start_level();
}

void World::_finalize()
{
	if (game_mode_defaults != nullptr && game_mode_defaults != game_mode)
	{
		memdelete(game_mode_defaults);
	}
	game_mode_defaults = nullptr;

	if (game_instance != nullptr)
	{
		game_instance->shutdown();
		memdelete(game_instance);
		game_instance = nullptr;
	}

	// Everything else is a node the tree owns and may already be gone by now, so
	// nothing more is freed here.
}

void World::create_game_instance()
{
	String game_instance_script_path = ProjectSettings::get_singleton()->get_setting("application/game_framework/game_instance_script", String());
	if (!game_instance_script_path.is_empty())
	{
		Ref<Script> game_instance_script = ResourceLoader::get_singleton()->load(game_instance_script_path);
		if (game_instance_script.is_valid())
		{
			game_instance = GFGD::try_create_instance_from<GameInstance>(game_instance_script);
		}

		if (game_instance == nullptr)
		{
			WARN_PRINT("GFGD: Could not create a GameInstance from \"application/game_framework/game_instance_script\". Falling back to the default GameInstance.");
		}
	}

	if (game_instance == nullptr)
	{
		game_instance = memnew(GameInstance);
	}
}

void World::create_net_driver()
{
	if (net_driver != nullptr) { return; }

	net_driver = memnew(NetDriver);
	net_driver->set_name("NetDriver");
	net_driver->set_world(this);
	get_root()->add_child(net_driver);
}

void World::create_input_router()
{
	if (input_router != nullptr) { return; }

	input_router = memnew(InputRouter);
	input_router->set_name("InputRouter");
	input_router->set_world(this);
	get_root()->add_child(input_router);
}

void World::create_containers()
{
	if (players_container == nullptr)
	{
		players_container = memnew(Node);
		players_container->set_name("Players");
		get_root()->add_child(players_container);
	}

	if (pawn_container == nullptr)
	{
		// Pawns hang off the world rather than off the level so that their path
		// does not change with the level's node name, which is what every remote
		// spawn and every synchronizer is addressed by. A plain Node parent also
		// leaves a 2D or 3D pawn's transform as its world transform.
		pawn_container = memnew(Node);
		pawn_container->set_name("Pawns");
		get_root()->add_child(pawn_container);
	}
}

Level* World::find_level()
{
	TypedArray<Node> root_children = get_root()->get_children();
	for (int i = 0; i < root_children.size(); i++)
	{
		if (Level* found_level = cast_to<Level>(root_children[i]))
		{
			return found_level;
		}
	}

	return nullptr;
}

Ref<PackedScene> World::resolve_game_mode_scene() const
{
	if (level != nullptr && level->get_game_mode_override().is_valid())
	{
		return level->get_game_mode_override();
	}

	String game_mode_path = ProjectSettings::get_singleton()->get_setting("application/game_framework/default_game_mode", String());
	if (!game_mode_path.is_empty())
	{
		Ref<PackedScene> game_mode_scene = ResourceLoader::get_singleton()->load(game_mode_path);
		if (game_mode_scene.is_valid())
		{
			return game_mode_scene;
		}

		WARN_PRINT("GFGD: \"application/game_framework/default_game_mode\" failed to load. Using an empty GameModeBase.");
	}

	return Ref<PackedScene>();
}

void World::resolve_game_mode(const Ref<PackedScene>& game_mode_scene)
{
	GameModeBase* instance = nullptr;

	if (game_mode_scene.is_valid())
	{
		Node* game_mode_node = game_mode_scene->instantiate();
		instance = Object::cast_to<GameModeBase>(game_mode_node);
		if (instance == nullptr && game_mode_node != nullptr)
		{
			WARN_PRINT("GFGD: The game mode scene root is not a GameModeBase. Falling back to the default GameModeBase.");
			memdelete(game_mode_node);
		}
	}

	if (instance == nullptr)
	{
		instance = memnew(GameModeBase);
	}

	instance->set_name("GameMode");

	game_mode_defaults = instance;

	// The rules run on the server only. On a client the same object is kept out
	// of the tree, where it never ticks and never decides anything - it is read
	// for the scenes the spawners have to agree on and nothing else.
	game_mode = has_authority() ? instance : nullptr;
}

void World::create_game_state()
{
	Ref<PackedScene> game_state_scene;
	if (game_mode_defaults != nullptr)
	{
		game_state_scene = game_mode_defaults->get_game_state_scene();
	}

	if (game_state_scene.is_valid())
	{
		Node* game_state_node = game_state_scene->instantiate();
		game_state = Object::cast_to<GameStateBase>(game_state_node);
		if (game_state == nullptr && game_state_node != nullptr)
		{
			WARN_PRINT("GFGD: game_state_scene root is not a GameStateBase. Falling back to the default GameStateBase.");
			memdelete(game_state_node);
		}
	}

	if (game_state == nullptr)
	{
		game_state = memnew(GameStateBase);
	}

	game_state->set_name("GameState");
}

void World::start_level()
{
	// The level starts first, so anything the game mode spawns joins a world that
	// is already live - and _init_game sees the level exactly as a level node's
	// own _ready did.
	if (level != nullptr && !level->is_inside_tree())
	{
		get_root()->add_child(level);
	}

	resolve_game_mode(resolve_game_mode_scene());

	// The game state is created before the game mode runs, because logging a
	// player in needs somewhere to attach a player state to - and because it is
	// the one part of the rules a client also has.
	create_game_state();
	get_root()->add_child(game_state);
	game_state->init_game_state(this);

	if (game_mode != nullptr)
	{
		// init_game runs with the game mode still outside the scene tree, so its
		// own _ready lands after it - which makes _ready the point where the
		// level, the players and the game state are all up.
		game_mode->init_game(this);
		get_root()->add_child(game_mode);
	}

	if (level != nullptr)
	{
		level->init_level(this);
	}

	emit_signal("level_loaded", level);
}

void World::destroy_level_nodes()
{
	// remove_child before queue_free, not queue_free alone: queue_free is deferred
	// to the end of the frame, so the old nodes would still be children of the
	// root - keeping their names, still running _process, and still listed as
	// controllers - while the new level is being built. remove_child runs
	// _exit_tree immediately, which is what unregisters the old controllers.
	auto discard = [](Node* node)
	{
		if (node == nullptr) { return; }
		if (node->get_parent() != nullptr)
		{
			node->get_parent()->remove_child(node);
		}
		node->queue_free();
	};

	auto discard_children = [&discard](Node* parent)
	{
		if (parent == nullptr) { return; }
		TypedArray<Node> children = parent->get_children();
		for (int i = 0; i < children.size(); i++)
		{
			discard(Object::cast_to<Node>(children[i]));
		}
	};

	discard_children(players_container);
	discard_children(pawn_container);

	if (net_driver != nullptr)
	{
		net_driver->clear_records();
	}

	if (game_mode_defaults != nullptr && game_mode_defaults != game_mode)
	{
		memdelete(game_mode_defaults);
	}
	game_mode_defaults = nullptr;

	discard(game_mode);
	game_mode = nullptr;

	discard(game_state);
	game_state = nullptr;

	discard(level);
	level = nullptr;

	controller_list.clear();
	player_controller_list.clear();
}

void World::load_level(const String& resource_path)
{
	Ref<PackedScene> level_packed_scene = ResourceLoader::get_singleton()->load(resource_path);
	if (level_packed_scene.is_null())
	{
		ERR_PRINT(vformat("GFGD: load_level failed to load \"%s\".", resource_path));
		return;
	}

	Node* level_node = level_packed_scene->instantiate();
	Level* new_level = cast_to<Level>(level_node);
	if (new_level == nullptr)
	{
		ERR_PRINT(vformat("GFGD: the scene root of \"%s\" is not a Level node.", resource_path));
		if (level_node != nullptr)
		{
			memdelete(level_node);
		}
		return;
	}

	// Local players outlive the level, but their controllers do not. Cut the link
	// before the old controllers are freed so nothing is left pointing at a dead
	// one, and release whatever was being held on the old level's devices.
	if (game_instance != nullptr)
	{
		for (int i = 0; i < game_instance->get_local_player_count(); i++)
		{
			LocalPlayer* local_player = game_instance->get_local_player(i);
			if (local_player == nullptr) { continue; }

			local_player->set_player_controller(nullptr);
			local_player->set_viewport_override(nullptr);
			if (PlayerInput* player_input = local_player->get_player_input())
			{
				player_input->reset_action_states();
				player_input->refresh_action_cache();
			}
		}
	}

	destroy_level_nodes();

	level = new_level;
	start_level();
}

void World::open_level(const String& resource_path)
{
	if (is_client())
	{
		ERR_PRINT("GFGD: a client cannot change level on its own - the server decides with server_travel.");
		return;
	}

	if (is_networked())
	{
		server_travel(resource_path);
		return;
	}

	load_level(resource_path);
}

void World::server_travel(const String& resource_path)
{
	if (!has_authority())
	{
		ERR_PRINT("GFGD: server_travel is the server's call.");
		return;
	}

	// The clients are told first, and stop counting as ready the moment they are:
	// anything the new level spawns here would otherwise be sent to a client that
	// is still standing in the old one.
	if (net_driver != nullptr)
	{
		net_driver->begin_travel(resource_path);
	}

	load_level(resource_path);
}

void World::register_controller(Controller* controller)
{
	if (controller == nullptr) { return; }
	if (controller_list.has(controller)) { return; }

	controller_list.append(controller);

	if (Object::cast_to<PlayerController>(controller) != nullptr)
	{
		player_controller_list.append(controller);
	}
}

void World::unregister_controller(Controller* controller)
{
	if (controller == nullptr) { return; }

	controller_list.erase(controller);
	player_controller_list.erase(controller);
}

void World::prune_controller_lists()
{
	// Self-unregistration in _exit_tree normally keeps these clean; this is the
	// belt and braces for a controller freed some other way.
	for (int i = controller_list.size() - 1; i >= 0; i--)
	{
		if (controller_list[i].get_validated_object() == nullptr)
		{
			controller_list.remove_at(i);
		}
	}

	for (int i = player_controller_list.size() - 1; i >= 0; i--)
	{
		if (player_controller_list[i].get_validated_object() == nullptr)
		{
			player_controller_list.remove_at(i);
		}
	}
}

Array World::get_controllers() const
{
	const_cast<World*>(this)->prune_controller_lists();
	return controller_list;
}

Array World::get_player_controllers() const
{
	const_cast<World*>(this)->prune_controller_lists();
	return player_controller_list;
}

PlayerController* World::get_first_player_controller() const
{
	return get_player_controller_at(0);
}

PlayerController* World::get_player_controller_at(int index) const
{
	const_cast<World*>(this)->prune_controller_lists();
	if (index < 0 || index >= player_controller_list.size()) { return nullptr; }

	return Object::cast_to<PlayerController>(player_controller_list[index].get_validated_object());
}

int World::get_player_controller_count() const
{
	const_cast<World*>(this)->prune_controller_lists();
	return player_controller_list.size();
}

PlayerController* World::find_player_controller_by_player_id(int player_id) const
{
	const_cast<World*>(this)->prune_controller_lists();
	for (int i = 0; i < player_controller_list.size(); i++)
	{
		PlayerController* player_controller = Object::cast_to<PlayerController>(player_controller_list[i].get_validated_object());
		if (player_controller != nullptr && player_controller->get_player_id() == player_id)
		{
			return player_controller;
		}
	}

	return nullptr;
}

LocalPlayer* World::create_local_player(int device_slot)
{
	return game_instance != nullptr ? game_instance->create_local_player(device_slot) : nullptr;
}

LocalPlayer* World::create_default_local_player()
{
	if (game_instance == nullptr) { return nullptr; }

	// DEVICE_SLOT_ALL is what makes a project that never touched per-player input
	// behave as it would without it: every device drives player 0, and
	// PlayerInput forwards every query straight to the Input singleton.
	return game_instance->create_local_player(PlayerInput::DEVICE_SLOT_ALL);
}

LocalPlayer* World::get_local_player(int index) const
{
	return game_instance != nullptr ? game_instance->get_local_player(index) : nullptr;
}

int World::get_local_player_count() const
{
	return game_instance != nullptr ? game_instance->get_local_player_count() : 0;
}

// --- Session ----------------------------------------------------------------

Error World::host_game(int port, int max_players)
{
	Ref<ENetMultiplayerPeer> peer;
	peer.instantiate();

	const Error error = peer->create_server(port, max_players);
	if (error != OK)
	{
		ERR_PRINT(vformat("GFGD: could not host on port %d (error %d).", port, (int)error));
		return error;
	}

	get_multiplayer()->set_multiplayer_peer(peer);
	emit_signal("net_mode_changed", get_net_mode());

	return OK;
}

Error World::create_dedicated_server(int port, int max_players)
{
	dedicated_server = true;

	const Error error = host_game(port, max_players);
	if (error != OK)
	{
		dedicated_server = false;
		return error;
	}

	// A server with nobody at the keyboard has no use for device routing, and a
	// local player here would spawn a pawn nobody is playing.
	//
	// The router clears the world's pointer to itself as it leaves the tree, so
	// the node has to be held onto here before that happens.
	if (InputRouter* router = input_router)
	{
		if (router->get_parent() != nullptr)
		{
			router->get_parent()->remove_child(router);
		}
		router->queue_free();
		input_router = nullptr;
	}

	return OK;
}

Error World::join_game(const String& address, int port)
{
	Ref<ENetMultiplayerPeer> peer;
	peer.instantiate();

	const Error error = peer->create_client(address, port);
	if (error != OK)
	{
		ERR_PRINT(vformat("GFGD: could not connect to %s:%d (error %d).", address, port, (int)error));
		return error;
	}

	get_multiplayer()->set_multiplayer_peer(peer);
	emit_signal("net_mode_changed", get_net_mode());

	return OK;
}

void World::disconnect_from_network()
{
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	if (multiplayer.is_null()) { return; }

	if (Ref<MultiplayerPeer> peer = multiplayer->get_multiplayer_peer(); peer.is_valid())
	{
		peer->close();
	}

	multiplayer->set_multiplayer_peer(Ref<MultiplayerPeer>());
	dedicated_server = false;

	emit_signal("net_mode_changed", get_net_mode());
}

bool World::has_authority() const
{
	if (!is_networked()) { return true; }

	return get_multiplayer()->is_server();
}

int World::get_local_peer_id() const
{
	if (!is_networked()) { return SERVER_PEER_ID; }

	return get_multiplayer()->get_unique_id();
}

bool World::is_networked() const
{
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	if (multiplayer.is_null() || !multiplayer->has_multiplayer_peer()) { return false; }

	// A tree always has a peer: without one of its own it is handed a stand-in
	// that answers "you are the server, and you are alone". That is a game with
	// no network, not a session of one.
	Ref<MultiplayerPeer> peer = multiplayer->get_multiplayer_peer();
	if (peer.is_null() || Object::cast_to<OfflineMultiplayerPeer>(peer.ptr()) != nullptr) { return false; }

	return peer->get_connection_status() != MultiplayerPeer::CONNECTION_DISCONNECTED;
}

World::NetMode World::get_net_mode() const
{
	if (!is_networked()) { return NET_MODE_STANDALONE; }
	if (!has_authority()) { return NET_MODE_CLIENT; }

	return dedicated_server ? NET_MODE_DEDICATED_SERVER : NET_MODE_LISTEN_SERVER;
}

bool World::is_dedicated_server() const
{
	return dedicated_server;
}

bool World::is_listen_server() const
{
	return get_net_mode() == NET_MODE_LISTEN_SERVER;
}

bool World::is_client() const
{
	return get_net_mode() == NET_MODE_CLIENT;
}

// --- Ownership and roles ----------------------------------------------------

int World::get_net_owner_peer(Node* node) const
{
	if (node == nullptr) { return SERVER_PEER_ID; }

	// A pawn's root is the node a game works with, but the framework's part of it
	// is the Pawn hanging underneath, so look one step down before climbing.
	if (Object::cast_to<Pawn>(node) == nullptr)
	{
		TypedArray<Node> children = node->get_children();
		for (int i = 0; i < children.size(); i++)
		{
			if (Pawn* child_pawn = Object::cast_to<Pawn>(children[i]))
			{
				node = child_pawn;
				break;
			}
		}
	}

	for (Node* current = node; current != nullptr; current = current->get_parent())
	{
		if (Pawn* pawn = Object::cast_to<Pawn>(current))
		{
			Controller* controller = pawn->get_controller();
			// An unpossessed pawn belongs to nobody, which is exactly what makes a
			// remote call on it from a client something to drop.
			return controller != nullptr ? get_net_owner_peer(controller) : SERVER_PEER_ID;
		}

		if (PlayerController* player_controller = Object::cast_to<PlayerController>(current))
		{
			return player_controller->get_owner_peer_id();
		}

		if (Object::cast_to<Controller>(current) != nullptr)
		{
			// An AI controller answers to the server and to nobody else.
			return SERVER_PEER_ID;
		}

		if (PlayerState* player_state = Object::cast_to<PlayerState>(current))
		{
			return player_state->get_unique_id();
		}
	}

	return SERVER_PEER_ID;
}

World::NetRole World::get_local_role_for(Node* node) const
{
	if (node == nullptr) { return ROLE_NONE; }
	if (has_authority()) { return ROLE_AUTHORITY; }

	return get_net_owner_peer(node) == get_local_peer_id() ? ROLE_AUTONOMOUS_PROXY : ROLE_SIMULATED_PROXY;
}

World::NetRole World::get_remote_role_for(Node* node) const
{
	if (node == nullptr) { return ROLE_NONE; }
	if (!has_authority()) { return ROLE_AUTHORITY; }
	if (!is_networked()) { return ROLE_NONE; }

	return get_net_owner_peer(node) != SERVER_PEER_ID ? ROLE_AUTONOMOUS_PROXY : ROLE_SIMULATED_PROXY;
}

void World::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_game_instance"), &World::get_game_instance);
	ClassDB::bind_method(D_METHOD("get_game_mode"), &World::get_game_mode);
	ClassDB::bind_method(D_METHOD("get_game_mode_defaults"), &World::get_game_mode_defaults);
	ClassDB::bind_method(D_METHOD("get_game_state"), &World::get_game_state);
	ClassDB::bind_method(D_METHOD("get_level"), &World::get_level);
	ClassDB::bind_method(D_METHOD("get_input_router"), &World::get_input_router);
	ClassDB::bind_method(D_METHOD("get_net_driver"), &World::get_net_driver);
	ClassDB::bind_method(D_METHOD("get_players_container"), &World::get_players_container);
	ClassDB::bind_method(D_METHOD("get_pawn_container"), &World::get_pawn_container);

	ClassDB::bind_method(D_METHOD("find_level"), &World::find_level);
	ClassDB::bind_method(D_METHOD("open_level", "resource_path"), &World::open_level);
	ClassDB::bind_method(D_METHOD("load_level", "resource_path"), &World::load_level);
	ClassDB::bind_method(D_METHOD("server_travel", "resource_path"), &World::server_travel);

	ClassDB::bind_method(D_METHOD("register_controller", "controller"), &World::register_controller);
	ClassDB::bind_method(D_METHOD("unregister_controller", "controller"), &World::unregister_controller);
	ClassDB::bind_method(D_METHOD("get_controllers"), &World::get_controllers);
	ClassDB::bind_method(D_METHOD("get_player_controllers"), &World::get_player_controllers);
	ClassDB::bind_method(D_METHOD("get_first_player_controller"), &World::get_first_player_controller);
	ClassDB::bind_method(D_METHOD("get_player_controller_at", "index"), &World::get_player_controller_at);
	ClassDB::bind_method(D_METHOD("get_player_controller_count"), &World::get_player_controller_count);
	ClassDB::bind_method(D_METHOD("find_player_controller_by_player_id", "player_id"), &World::find_player_controller_by_player_id);

	ClassDB::bind_method(D_METHOD("create_local_player", "device_slot"), &World::create_local_player);
	ClassDB::bind_method(D_METHOD("create_default_local_player"), &World::create_default_local_player);
	ClassDB::bind_method(D_METHOD("get_local_player", "index"), &World::get_local_player);
	ClassDB::bind_method(D_METHOD("get_local_player_count"), &World::get_local_player_count);

	ClassDB::bind_method(D_METHOD("host_game", "port", "max_players"), &World::host_game, DEFVAL(32));
	ClassDB::bind_method(D_METHOD("create_dedicated_server", "port", "max_players"), &World::create_dedicated_server, DEFVAL(32));
	ClassDB::bind_method(D_METHOD("join_game", "address", "port"), &World::join_game);
	ClassDB::bind_method(D_METHOD("disconnect_from_network"), &World::disconnect_from_network);

	ClassDB::bind_method(D_METHOD("get_net_mode"), &World::get_net_mode);
	ClassDB::bind_method(D_METHOD("is_dedicated_server"), &World::is_dedicated_server);
	ClassDB::bind_method(D_METHOD("is_listen_server"), &World::is_listen_server);
	ClassDB::bind_method(D_METHOD("is_client"), &World::is_client);
	ClassDB::bind_method(D_METHOD("is_networked"), &World::is_networked);
	ClassDB::bind_method(D_METHOD("has_authority"), &World::has_authority);
	ClassDB::bind_method(D_METHOD("get_local_peer_id"), &World::get_local_peer_id);

	ClassDB::bind_method(D_METHOD("get_net_owner_peer", "node"), &World::get_net_owner_peer);
	ClassDB::bind_method(D_METHOD("get_local_role_for", "node"), &World::get_local_role_for);
	ClassDB::bind_method(D_METHOD("get_remote_role_for", "node"), &World::get_remote_role_for);

	BIND_ENUM_CONSTANT(NET_MODE_STANDALONE);
	BIND_ENUM_CONSTANT(NET_MODE_DEDICATED_SERVER);
	BIND_ENUM_CONSTANT(NET_MODE_LISTEN_SERVER);
	BIND_ENUM_CONSTANT(NET_MODE_CLIENT);

	BIND_ENUM_CONSTANT(ROLE_NONE);
	BIND_ENUM_CONSTANT(ROLE_SIMULATED_PROXY);
	BIND_ENUM_CONSTANT(ROLE_AUTONOMOUS_PROXY);
	BIND_ENUM_CONSTANT(ROLE_AUTHORITY);

	BIND_CONSTANT(SERVER_PEER_ID);

	ADD_SIGNAL(MethodInfo("level_loaded", PropertyInfo(Variant::OBJECT, "level", PROPERTY_HINT_NODE_TYPE, "Level")));
	ADD_SIGNAL(MethodInfo("net_mode_changed", PropertyInfo(Variant::INT, "net_mode")));
	ADD_SIGNAL(MethodInfo("connected_to_server"));
	ADD_SIGNAL(MethodInfo("connection_failed"));
	ADD_SIGNAL(MethodInfo("server_disconnected"));
	ADD_SIGNAL(MethodInfo("peer_connected", PropertyInfo(Variant::INT, "peer_id")));
	ADD_SIGNAL(MethodInfo("peer_disconnected", PropertyInfo(Variant::INT, "peer_id")));
}
}
