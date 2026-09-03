#include "framework/net_driver.h"

#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/game_mode_base.h"
#include "framework/game_state_base.h"
#include "framework/level.h"
#include "framework/local_player.h"
#include "framework/pawn.h"
#include "framework/player_controller.h"
#include "framework/player_state.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
namespace
{
Dictionary make_rpc_config(MultiplayerAPI::RPCMode mode, MultiplayerPeer::TransferMode transfer)
{
	Dictionary config;
	config["rpc_mode"] = mode;
	config["transfer_mode"] = transfer;
	config["call_local"] = false;
	config["channel"] = 0;
	return config;
}
}

NetDriver::NetDriver()
{
	world = nullptr;
}

NetDriver::~NetDriver()
{

}

void NetDriver::_ready()
{
	if (world == nullptr)
	{
		world = Object::cast_to<World>(get_tree());
	}

	configure_rpcs();

	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	if (multiplayer.is_null()) { return; }

	multiplayer->connect("peer_connected", callable_mp(this, &NetDriver::on_peer_connected));
	multiplayer->connect("peer_disconnected", callable_mp(this, &NetDriver::on_peer_disconnected));
	multiplayer->connect("connected_to_server", callable_mp(this, &NetDriver::on_connected_to_server));
	multiplayer->connect("connection_failed", callable_mp(this, &NetDriver::on_connection_failed));
	multiplayer->connect("server_disconnected", callable_mp(this, &NetDriver::on_server_disconnected));
}

void NetDriver::configure_rpcs()
{
	const Dictionary from_server = make_rpc_config(MultiplayerAPI::RPC_MODE_AUTHORITY, MultiplayerPeer::TRANSFER_MODE_RELIABLE);
	const Dictionary from_anyone = make_rpc_config(MultiplayerAPI::RPC_MODE_ANY_PEER, MultiplayerPeer::TRANSFER_MODE_RELIABLE);

	rpc_config("client_travel_to_level", from_server);
	rpc_config("client_spawn_player_state", from_server);
	rpc_config("client_despawn_player_state", from_server);
	rpc_config("client_spawn_pawn", from_server);
	rpc_config("client_despawn_pawn", from_server);
	rpc_config("client_create_player_controller", from_server);
	rpc_config("client_destroy_player_controller", from_server);

	rpc_config("server_notify_level_loaded", from_anyone);
}

// --- Connections -------------------------------------------------------------

void NetDriver::on_peer_connected(int peer_id)
{
	if (world == nullptr) { return; }

	world->emit_signal("peer_connected", peer_id);

	if (!world->has_authority()) { return; }

	// A peer that just arrived is standing in whatever level it started in, so
	// the first thing it is told is which one to be in. Nothing is sent to it
	// until it reports back that the level is up.
	Level* level = world->get_level();
	const String level_path = level != nullptr ? level->get_scene_file_path() : String();
	if (level_path.is_empty())
	{
		ERR_PRINT("GFGD: the current level was not loaded from a scene file, so a joining client cannot be told what to load.");
		return;
	}

	rpc_id(peer_id, "client_travel_to_level", level_path);
}

void NetDriver::on_peer_disconnected(int peer_id)
{
	if (world == nullptr) { return; }

	world->emit_signal("peer_disconnected", peer_id);

	const int64_t index = ready_peers.find(peer_id);
	if (index >= 0)
	{
		ready_peers.remove_at(index);
	}

	if (!world->has_authority()) { return; }

	if (GameModeBase* game_mode = world->get_game_mode())
	{
		game_mode->logout_peer(peer_id);
	}
}

void NetDriver::on_connected_to_server()
{
	if (world != nullptr)
	{
		world->emit_signal("connected_to_server");
	}
}

void NetDriver::on_connection_failed()
{
	if (world == nullptr) { return; }

	world->disconnect_from_network();
	world->emit_signal("connection_failed");
}

void NetDriver::on_server_disconnected()
{
	if (world == nullptr) { return; }

	world->disconnect_from_network();
	world->emit_signal("server_disconnected");
}

// --- Travel ------------------------------------------------------------------

void NetDriver::begin_travel(const String& level_path)
{
	if (world == nullptr || !world->has_authority()) { return; }

	ready_peers.clear();
	clear_records();

	if (!world->is_networked()) { return; }

	// Every connected peer, not only the ones that were ready: a peer still
	// loading the level it was told about a moment ago would otherwise finish
	// into a level the server has already left.
	rpc("client_travel_to_level", level_path);
}

void NetDriver::client_travel_to_level(const String& level_path)
{
	if (world == nullptr || world->has_authority()) { return; }

	world->load_level(level_path);

	rpc_id(World::SERVER_PEER_ID, "server_notify_level_loaded");
}

void NetDriver::server_notify_level_loaded()
{
	if (world == nullptr || !world->has_authority()) { return; }

	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int peer_id = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	if (peer_id <= 0) { return; }

	if (!ready_peers.has(peer_id))
	{
		ready_peers.push_back(peer_id);
	}

	// The peer is handed the level as it stands before it is given a player of
	// its own, so its own controller finds a world that is already populated.
	send_world_state_to(peer_id);

	if (GameModeBase* game_mode = world->get_game_mode())
	{
		game_mode->login_peer(peer_id);
	}
}

bool NetDriver::is_peer_ready(int peer_id) const
{
	return ready_peers.has(peer_id);
}

void NetDriver::clear_records()
{
	player_state_records.clear();
	pawn_records.clear();
}

void NetDriver::send_world_state_to(int peer_id)
{
	Array player_ids = player_state_records.keys();
	for (int i = 0; i < player_ids.size(); i++)
	{
		rpc_id(peer_id, "client_spawn_player_state", player_state_records[player_ids[i]]);
	}

	Array pawn_ids = pawn_records.keys();
	for (int i = 0; i < pawn_ids.size(); i++)
	{
		Dictionary record = pawn_records[pawn_ids[i]];

		// Send where the pawn is now, not where it started: a peer joining an hour
		// in should see the game as it stands.
		if (Node* pawn_root = find_pawn_root((int)pawn_ids[i]))
		{
			if (Node3D* pawn_3d = Object::cast_to<Node3D>(pawn_root))
			{
				record["transform"] = pawn_3d->get_transform();
			}
			else if (Node2D* pawn_2d = Object::cast_to<Node2D>(pawn_root))
			{
				record["transform"] = pawn_2d->get_transform();
			}
		}

		rpc_id(peer_id, "client_spawn_pawn", record);
	}
}

void NetDriver::send_to_ready_peers(const StringName& method, const Variant& argument)
{
	if (world == nullptr || !world->is_networked() || !world->has_authority()) { return; }

	for (int i = 0; i < ready_peers.size(); i++)
	{
		rpc_id(ready_peers[i], method, argument);
	}
}

// --- Player states -----------------------------------------------------------

PlayerState* NetDriver::spawn_player_state(int player_id, int owner_peer, const String& player_name, int player_index, bool bot)
{
	Ref<PackedScene> player_state_scene;
	if (GameModeBase* defaults = world != nullptr ? world->get_game_mode_defaults() : nullptr)
	{
		player_state_scene = defaults->get_player_state_scene();
	}

	Dictionary data;
	data["player_id"] = player_id;
	data["peer_id"] = owner_peer;
	data["player_name"] = player_name;
	data["player_index"] = player_index;
	data["bot"] = bot;
	data["scene_path"] = scene_path_or_warn(player_state_scene, "player_state_scene");

	PlayerState* player_state = build_player_state(data, player_state_scene);
	if (player_state == nullptr) { return nullptr; }

	player_state_records[player_id] = data;
	send_to_ready_peers("client_spawn_player_state", data);

	return player_state;
}

void NetDriver::client_spawn_player_state(const Dictionary& data)
{
	if (world == nullptr || world->has_authority()) { return; }

	build_player_state(data, Ref<PackedScene>());
}

PlayerState* NetDriver::build_player_state(const Dictionary& data, const Ref<PackedScene>& scene_hint)
{
	if (world == nullptr) { return nullptr; }

	GameStateBase* game_state = world->get_game_state();
	if (game_state == nullptr)
	{
		WARN_PRINT("GFGD: there is no GameState, so no PlayerState was created.");
		return nullptr;
	}

	const int player_id = data.get("player_id", 0);
	const int peer_id = data.get("peer_id", World::SERVER_PEER_ID);

	Ref<PackedScene> player_state_scene = scene_hint;
	if (player_state_scene.is_null())
	{
		const String scene_path = data.get("scene_path", String());
		if (!scene_path.is_empty())
		{
			player_state_scene = ResourceLoader::get_singleton()->load(scene_path);
		}
	}

	PlayerState* player_state = nullptr;
	if (player_state_scene.is_valid())
	{
		Node* player_state_node = player_state_scene->instantiate();
		player_state = Object::cast_to<PlayerState>(player_state_node);
		if (player_state == nullptr && player_state_node != nullptr)
		{
			WARN_PRINT("GFGD: player_state_scene root is not a PlayerState. Using a default PlayerState instead.");
			memdelete(player_state_node);
		}
	}

	if (player_state == nullptr)
	{
		player_state = memnew(PlayerState);
	}

	player_state->set_name(vformat("PlayerState%d", player_id));
	player_state->set_player_id(player_id);
	player_state->set_unique_id(peer_id);
	player_state->set_player_index(data.get("player_index", 0));
	player_state->set_a_bot(data.get("bot", false));
	player_state->set_local(peer_id == world->get_local_peer_id());

	const String player_name = data.get("player_name", String());
	if (!player_name.is_empty())
	{
		player_state->set_player_name(player_name);
	}
	else if (player_state->get_player_name().is_empty())
	{
		player_state->set_player_name(vformat("Player %d", player_id));
	}

	game_state->add_player_state(player_state);

	// A controller that arrived first is still waiting for this.
	if (PlayerController* player_controller = world->find_player_controller_by_player_id(player_id))
	{
		player_controller->set_player_state(player_state);
	}

	return player_state;
}

void NetDriver::despawn_player_state(int player_id)
{
	client_despawn_player_state(player_id);

	player_state_records.erase(player_id);
	send_to_ready_peers("client_despawn_player_state", player_id);
}

void NetDriver::client_despawn_player_state(int player_id)
{
	if (world == nullptr) { return; }

	GameStateBase* game_state = world->get_game_state();
	if (game_state == nullptr) { return; }

	game_state->remove_player_state(game_state->get_player_state_by_player_id(player_id));
}

// --- Player controllers ------------------------------------------------------

PlayerController* NetDriver::spawn_player_controller(int player_id, int owner_peer)
{
	Ref<PackedScene> player_controller_scene;
	if (GameModeBase* defaults = world != nullptr ? world->get_game_mode_defaults() : nullptr)
	{
		player_controller_scene = defaults->get_player_controller_scene();
	}

	Dictionary data;
	data["player_id"] = player_id;
	data["peer_id"] = owner_peer;
	data["scene_path"] = scene_path_or_warn(player_controller_scene, "player_controller_scene");

	PlayerController* player_controller = build_player_controller(data, player_controller_scene);
	if (player_controller == nullptr) { return nullptr; }

	// This is the one thing a client only gets a copy of when it is its own: a
	// player controller is the connection's, and nobody else has any business
	// holding it.
	if (owner_peer != World::SERVER_PEER_ID && is_peer_ready(owner_peer))
	{
		rpc_id(owner_peer, "client_create_player_controller", data);
	}

	return player_controller;
}

void NetDriver::client_create_player_controller(const Dictionary& data)
{
	if (world == nullptr || world->has_authority()) { return; }

	build_player_controller(data, Ref<PackedScene>());
}

PlayerController* NetDriver::build_player_controller(const Dictionary& data, const Ref<PackedScene>& scene_hint)
{
	if (world == nullptr) { return nullptr; }

	Node* parent = world->get_players_container();
	if (parent == nullptr) { return nullptr; }

	const int player_id = data.get("player_id", 0);
	const int peer_id = data.get("peer_id", World::SERVER_PEER_ID);

	Ref<PackedScene> player_controller_scene = scene_hint;
	if (player_controller_scene.is_null())
	{
		const String scene_path = data.get("scene_path", String());
		if (!scene_path.is_empty())
		{
			player_controller_scene = ResourceLoader::get_singleton()->load(scene_path);
		}
	}

	PlayerController* player_controller = nullptr;
	if (player_controller_scene.is_valid())
	{
		Node* controller_node = player_controller_scene->instantiate();
		player_controller = Object::cast_to<PlayerController>(controller_node);
		if (player_controller == nullptr && controller_node != nullptr)
		{
			WARN_PRINT("GFGD: player_controller_scene root is not a PlayerController. Using a default PlayerController instead.");
			memdelete(controller_node);
		}
	}

	if (player_controller == nullptr)
	{
		player_controller = memnew(PlayerController);
	}

	player_controller->set_name(vformat("PlayerController%d", player_id));
	player_controller->set_player_id(player_id);
	player_controller->set_owner_peer_id(peer_id);

	parent->add_child(player_controller);

	if (!world->has_authority())
	{
		// A client only ever builds its own, and it is the human at this machine
		// who drives it - so it reads this machine's input.
		LocalPlayer* local_player = world->get_local_player(0);
		if (local_player == nullptr)
		{
			local_player = world->create_default_local_player();
		}
		player_controller->set_local_player(local_player);

		if (GameStateBase* game_state = world->get_game_state())
		{
			player_controller->set_player_state(game_state->get_player_state_by_player_id(player_id));
		}

		if (Node* pawn_root = find_pawn_root(player_id))
		{
			if (Pawn* pawn = Pawn::find_in(pawn_root))
			{
				player_controller->possess(pawn);
			}
		}
	}

	return player_controller;
}

void NetDriver::despawn_player_controller(int player_id)
{
	if (world == nullptr) { return; }

	PlayerController* player_controller = world->find_player_controller_by_player_id(player_id);
	const int owner_peer = player_controller != nullptr ? player_controller->get_owner_peer_id() : World::SERVER_PEER_ID;

	if (player_controller != nullptr)
	{
		if (player_controller->get_parent() != nullptr)
		{
			player_controller->get_parent()->remove_child(player_controller);
		}
		player_controller->queue_free();
	}

	if (owner_peer != World::SERVER_PEER_ID && is_peer_ready(owner_peer))
	{
		rpc_id(owner_peer, "client_destroy_player_controller", player_id);
	}
}

void NetDriver::client_destroy_player_controller(int player_id)
{
	if (world == nullptr || world->has_authority()) { return; }

	if (PlayerController* player_controller = world->find_player_controller_by_player_id(player_id))
	{
		if (player_controller->get_parent() != nullptr)
		{
			player_controller->get_parent()->remove_child(player_controller);
		}
		player_controller->queue_free();
	}
}

// --- Pawns -------------------------------------------------------------------

Node* NetDriver::spawn_pawn(int player_id, const Ref<PackedScene>& pawn_scene, const Variant& spawn_transform)
{
	if (pawn_scene.is_null())
	{
		WARN_PRINT("GFGD: no pawn scene to spawn.");
		return nullptr;
	}

	Dictionary data;
	data["player_id"] = player_id;
	data["scene_path"] = scene_path_or_warn(pawn_scene, "the pawn scene");
	data["transform"] = spawn_transform;

	Node* pawn_root = build_pawn(data, pawn_scene);
	if (pawn_root == nullptr) { return nullptr; }

	pawn_records[player_id] = data;
	send_to_ready_peers("client_spawn_pawn", data);

	return pawn_root;
}

void NetDriver::client_spawn_pawn(const Dictionary& data)
{
	if (world == nullptr || world->has_authority()) { return; }

	build_pawn(data, Ref<PackedScene>());
}

Node* NetDriver::build_pawn(const Dictionary& data, const Ref<PackedScene>& scene_hint)
{
	if (world == nullptr) { return nullptr; }

	Node* parent = world->get_pawn_container();
	if (parent == nullptr) { return nullptr; }

	const int player_id = data.get("player_id", 0);

	Ref<PackedScene> pawn_scene = scene_hint;
	if (pawn_scene.is_null())
	{
		const String scene_path = data.get("scene_path", String());
		if (!scene_path.is_empty())
		{
			pawn_scene = ResourceLoader::get_singleton()->load(scene_path);
		}
	}

	if (pawn_scene.is_null())
	{
		ERR_PRINT(vformat("GFGD: could not load the pawn scene for player %d.", player_id));
		return nullptr;
	}

	Node* pawn_root = pawn_scene->instantiate();
	if (pawn_root == nullptr) { return nullptr; }

	// The name is derived from the player id rather than from the scene, because
	// it is the address every peer refers to this pawn by.
	pawn_root->set_name(vformat("Pawn%d", player_id));
	parent->add_child(pawn_root);

	// The container is a plain Node, so a pawn's own transform is its world
	// transform and no parent transform has to be unwound.
	const Variant spawn_transform = data.get("transform", Variant());
	if (spawn_transform.get_type() == Variant::TRANSFORM3D)
	{
		if (Node3D* pawn_3d = Object::cast_to<Node3D>(pawn_root))
		{
			pawn_3d->set_transform(spawn_transform);
		}
	}
	else if (spawn_transform.get_type() == Variant::TRANSFORM2D)
	{
		if (Node2D* pawn_2d = Object::cast_to<Node2D>(pawn_root))
		{
			pawn_2d->set_transform(spawn_transform);
		}
	}

	Pawn* pawn = Pawn::find_in(pawn_root);
	if (pawn == nullptr)
	{
		WARN_PRINT("GFGD: the pawn scene has no Pawn node, so it cannot be possessed.");
		return pawn_root;
	}

	pawn->set_player_id(player_id);

	// Possession happens wherever the controller for this player exists: on the
	// server for everyone, and on a client for its own.
	if (PlayerController* player_controller = world->find_player_controller_by_player_id(player_id))
	{
		player_controller->possess(pawn);
	}

	return pawn_root;
}

void NetDriver::despawn_pawn(int player_id)
{
	client_despawn_pawn(player_id);

	pawn_records.erase(player_id);
	send_to_ready_peers("client_despawn_pawn", player_id);
}

void NetDriver::client_despawn_pawn(int player_id)
{
	Node* pawn_root = find_pawn_root(player_id);
	if (pawn_root == nullptr) { return; }

	if (Pawn* pawn = Pawn::find_in(pawn_root))
	{
		if (Controller* controller = pawn->get_controller())
		{
			controller->unpossess();
		}
	}

	if (pawn_root->get_parent() != nullptr)
	{
		pawn_root->get_parent()->remove_child(pawn_root);
	}
	pawn_root->queue_free();
}

Node* NetDriver::find_pawn_root(int player_id) const
{
	if (world == nullptr || world->get_pawn_container() == nullptr) { return nullptr; }

	return world->get_pawn_container()->get_node_or_null(NodePath(vformat("Pawn%d", player_id)));
}

String NetDriver::scene_path_or_warn(const Ref<PackedScene>& scene, const char* what) const
{
	if (scene.is_null()) { return String(); }

	const String scene_path = scene->get_path();
	if (scene_path.is_empty() && world != nullptr && world->is_networked())
	{
		WARN_PRINT(vformat("GFGD: %s was not loaded from a file, so a client cannot build the same thing. Save it as its own scene.", String(what)));
	}

	return scene_path;
}

void NetDriver::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_world"), &NetDriver::get_world);
	ClassDB::bind_method(D_METHOD("is_peer_ready", "peer_id"), &NetDriver::is_peer_ready);
	ClassDB::bind_method(D_METHOD("get_ready_peers"), &NetDriver::get_ready_peers);

	ClassDB::bind_method(D_METHOD("client_travel_to_level", "level_path"), &NetDriver::client_travel_to_level);
	ClassDB::bind_method(D_METHOD("server_notify_level_loaded"), &NetDriver::server_notify_level_loaded);
	ClassDB::bind_method(D_METHOD("client_spawn_player_state", "data"), &NetDriver::client_spawn_player_state);
	ClassDB::bind_method(D_METHOD("client_despawn_player_state", "player_id"), &NetDriver::client_despawn_player_state);
	ClassDB::bind_method(D_METHOD("client_spawn_pawn", "data"), &NetDriver::client_spawn_pawn);
	ClassDB::bind_method(D_METHOD("client_despawn_pawn", "player_id"), &NetDriver::client_despawn_pawn);
	ClassDB::bind_method(D_METHOD("client_create_player_controller", "data"), &NetDriver::client_create_player_controller);
	ClassDB::bind_method(D_METHOD("client_destroy_player_controller", "player_id"), &NetDriver::client_destroy_player_controller);
}
}
