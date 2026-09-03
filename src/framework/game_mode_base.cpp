#include "framework/game_mode_base.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>

#include "framework/game_instance.h"
#include "framework/game_state_base.h"
#include "framework/level.h"
#include "framework/local_player.h"
#include "framework/net_driver.h"
#include "framework/pawn.h"
#include "framework/player_controller.h"
#include "framework/player_input.h"
#include "framework/player_start_2d.h"
#include "framework/player_start_3d.h"
#include "framework/player_state.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
GameModeBase::GameModeBase()
{
	max_local_players = 1;
	allow_press_to_join = false;
	max_players = 8;
	world = nullptr;
}

GameModeBase::~GameModeBase()
{

}

void GameModeBase::init_game(World* in_world)
{
	world = in_world;

	GDVIRTUAL_CALL(_init_game_state);

	bool handled = false;
	GDVIRTUAL_CALL(_init_game, in_world, handled);

	start_play();

	if (!handled)
	{
		restart_all_players();
	}
}

void GameModeBase::start_play()
{
	if (world == nullptr) { return; }

	if (GameStateBase* game_state = world->get_game_state())
	{
		game_state->handle_begin_play();
	}

	emit_signal("play_started");
}

// --- Logging players in ------------------------------------------------------

PlayerController* GameModeBase::login_internal(int peer_id, LocalPlayer* local_player)
{
	if (world == nullptr)
	{
		ERR_PRINT("GFGD: the game mode has no world yet. Log players in from _init_game or later.");
		return nullptr;
	}

	if (!world->has_authority())
	{
		WARN_PRINT("GFGD: logging a player in is the server's call.");
		return nullptr;
	}

	NetDriver* net_driver = world->get_net_driver();
	if (net_driver == nullptr) { return nullptr; }

	const int player_id = world->take_next_player_id();

	// The controller comes first, so that when the player state and the pawn
	// arrive on the owning client there is already something to attach them to.
	PlayerController* player_controller = net_driver->spawn_player_controller(player_id, peer_id);
	if (player_controller == nullptr) { return nullptr; }

	// The LocalPlayer has to be attached before anything possesses or spawns: the
	// camera picks the player's viewport from it, and the input pump reads its
	// PlayerInput.
	if (local_player != nullptr)
	{
		player_controller->set_local_player(local_player);
	}

	const int player_index = local_player != nullptr ? local_player->get_player_index() : 0;
	PlayerState* player_state = net_driver->spawn_player_state(player_id, peer_id, vformat("Player %d", player_id), player_index, false);
	player_controller->set_player_state(player_state);

	restart_player(player_controller);

	GDVIRTUAL_CALL(_on_post_login, player_controller);
	emit_signal("player_logged_in", player_controller);

	return player_controller;
}

PlayerController* GameModeBase::login_local_player(LocalPlayer* local_player)
{
	if (local_player == nullptr)
	{
		ERR_PRINT("GFGD: login_local_player was given no LocalPlayer.");
		return nullptr;
	}

	if (local_player->get_player_controller() != nullptr)
	{
		return local_player->get_player_controller();
	}

	const int local_peer = world != nullptr ? world->get_local_peer_id() : World::SERVER_PEER_ID;
	return login_internal(local_peer, local_player);
}

PlayerController* GameModeBase::login_peer(int peer_id)
{
	if (world == nullptr || !world->has_authority()) { return nullptr; }

	String refusal;
	if (GDVIRTUAL_CALL(_pre_login, peer_id, refusal) && !refusal.is_empty())
	{
		refuse_peer(peer_id, refusal);
		return nullptr;
	}

	if (get_num_players() >= max_players)
	{
		refuse_peer(peer_id, "The session is full.");
		return nullptr;
	}

	return login_internal(peer_id, nullptr);
}

void GameModeBase::refuse_peer(int peer_id, const String& reason)
{
	WARN_PRINT(vformat("GFGD: refused peer %d: %s", peer_id, reason));

	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	if (multiplayer.is_null() || !multiplayer->has_multiplayer_peer()) { return; }

	Ref<MultiplayerPeer> peer = multiplayer->get_multiplayer_peer();
	if (peer.is_valid())
	{
		peer->disconnect_peer(peer_id);
	}

	emit_signal("login_refused", peer_id, reason);
}

void GameModeBase::logout(PlayerController* player_controller)
{
	if (player_controller == nullptr || world == nullptr) { return; }

	GDVIRTUAL_CALL(_on_logout, player_controller);
	emit_signal("player_logged_out", player_controller);

	release_player_start(player_controller);

	const int player_id = player_controller->get_player_id();
	player_controller->set_local_player(nullptr);
	player_controller->set_player_state(nullptr);

	if (NetDriver* net_driver = world->get_net_driver())
	{
		net_driver->despawn_pawn(player_id);
		net_driver->despawn_player_state(player_id);
		net_driver->despawn_player_controller(player_id);
	}
}

void GameModeBase::logout_peer(int peer_id)
{
	if (world == nullptr) { return; }

	Array player_controllers = world->get_player_controllers();
	for (int i = player_controllers.size() - 1; i >= 0; i--)
	{
		PlayerController* player_controller = Object::cast_to<PlayerController>(player_controllers[i].get_validated_object());
		if (player_controller != nullptr && player_controller->get_owner_peer_id() == peer_id)
		{
			logout(player_controller);
		}
	}
}

void GameModeBase::restart_all_players()
{
	if (world == nullptr)
	{
		ERR_PRINT("GFGD: restart_all_players has no world. Call it from _init_game or later.");
		return;
	}

	// Nobody is sitting at a dedicated server, so there is no local player to log
	// in - it waits for machines to connect instead.
	if (world->is_dedicated_server()) { return; }

	GameInstance* game_instance = world->get_game_instance();
	if (game_instance == nullptr) { return; }

	// A project that never touched the local player API has none yet, so make the
	// one that has always existed implicitly.
	if (game_instance->get_local_player_count() == 0)
	{
		world->create_default_local_player();
	}

	if (max_local_players > 1)
	{
		LocalPlayer* first = game_instance->get_local_player(0);
		if (first != nullptr && first->has_device_slot(PlayerInput::DEVICE_SLOT_ALL))
		{
			WARN_PRINT("GFGD: max_local_players is above 1 but local player 0 still accepts every device, which leaves nothing for the others. Give it a specific device slot (see PlayerInput.DeviceSlot).");
		}
	}

	const int login_count = Math::min(game_instance->get_local_player_count(), max_local_players);
	for (int i = 0; i < login_count; i++)
	{
		LocalPlayer* local_player = game_instance->get_local_player(i);
		if (local_player == nullptr || local_player->get_player_controller() != nullptr) { continue; }

		login_local_player(local_player);
	}
}

PlayerController* GameModeBase::spawn_default_player()
{
	if (world == nullptr)
	{
		ERR_PRINT("GFGD: spawn_default_player has no world. Call it from _init_game or later.");
		return nullptr;
	}

	GameInstance* game_instance = world->get_game_instance();
	if (game_instance == nullptr) { return nullptr; }

	if (game_instance->get_local_player_count() == 0)
	{
		world->create_default_local_player();
	}

	LocalPlayer* local_player = game_instance->get_local_player(0);
	if (local_player == nullptr) { return nullptr; }

	return login_local_player(local_player);
}

bool GameModeBase::try_join(int device_slot)
{
	if (world == nullptr || !allow_press_to_join) { return false; }

	GameInstance* game_instance = world->get_game_instance();
	if (game_instance == nullptr) { return false; }

	// An existing player with no device yet claims the slot before a new player is
	// made, so "press to join" also works as "press to pick up your pad again".
	LocalPlayer* waiting_player = nullptr;
	for (int i = 0; i < game_instance->get_local_player_count(); i++)
	{
		LocalPlayer* candidate = game_instance->get_local_player(i);
		if (candidate != nullptr && candidate->get_device_slots().is_empty())
		{
			waiting_player = candidate;
			break;
		}
	}

	if (waiting_player == nullptr && game_instance->get_local_player_count() >= max_local_players)
	{
		return false;
	}

	bool can_join = true;
	GDVIRTUAL_CALL(_can_join, device_slot, can_join);
	if (!can_join) { return false; }

	LocalPlayer* local_player = waiting_player;
	if (local_player != nullptr)
	{
		local_player->add_device_slot(device_slot);
	}
	else
	{
		local_player = game_instance->create_local_player(device_slot);
	}

	if (local_player == nullptr) { return false; }

	if (local_player->get_player_controller() == nullptr)
	{
		login_local_player(local_player);
	}

	return true;
}

// --- Spawning ----------------------------------------------------------------

Ref<PackedScene> GameModeBase::get_pawn_scene_for_player(int player_index) const
{
	if (player_index >= 0 && player_index < pawn_scene_overrides.size())
	{
		Ref<PackedScene> override_scene = pawn_scene_overrides[player_index];
		if (override_scene.is_valid())
		{
			return override_scene;
		}
	}

	return default_pawn_scene;
}

Ref<PackedScene> GameModeBase::get_pawn_scene_for(Controller* controller)
{
	Ref<PackedScene> chosen;
	if (GDVIRTUAL_CALL(_get_pawn_scene_for, controller, chosen) && chosen.is_valid())
	{
		return chosen;
	}

	PlayerController* as_player = Object::cast_to<PlayerController>(controller);
	const int player_index = as_player != nullptr ? Math::max(as_player->get_player_index(), 0) : 0;

	return get_pawn_scene_for_player(player_index);
}

Node* GameModeBase::find_player_start(const StringName& player_start_tag) const
{
	if (world == nullptr || world->get_level() == nullptr) { return nullptr; }

	Node* level = world->get_level();
	TypedArray<Node> candidates = level->find_children("*", "PlayerStart3D", true, false);
	candidates.append_array(level->find_children("*", "PlayerStart2D", true, false));

	for (int i = 0; i < candidates.size(); i++)
	{
		Node* candidate = Object::cast_to<Node>(candidates[i]);
		if (candidate == nullptr) { continue; }

		StringName tag;
		if (PlayerStart3D* start_3d = Object::cast_to<PlayerStart3D>(candidate))
		{
			tag = start_3d->get_player_start_tag();
		}
		else if (PlayerStart2D* start_2d = Object::cast_to<PlayerStart2D>(candidate))
		{
			tag = start_2d->get_player_start_tag();
		}

		if (tag == player_start_tag)
		{
			return candidate;
		}
	}

	return nullptr;
}

Node* GameModeBase::choose_player_start(Controller* controller)
{
	Node* chosen = nullptr;
	if (GDVIRTUAL_CALL(_choose_player_start, controller, chosen) && chosen != nullptr)
	{
		claimed_player_starts.append(chosen);
		return chosen;
	}

	if (world == nullptr || world->get_level() == nullptr) { return nullptr; }

	Node* level = world->get_level();
	TypedArray<Node> candidates = level->find_children("*", "PlayerStart3D", true, false);
	candidates.append_array(level->find_children("*", "PlayerStart2D", true, false));
	if (candidates.is_empty())
	{
		WARN_PRINT("GFGD: this level has no PlayerStart2D or PlayerStart3D, so pawns spawn at the origin.");
		return nullptr;
	}

	// Dead entries go first: a start claimed by a pawn that has since been freed
	// is available again.
	for (int i = claimed_player_starts.size() - 1; i >= 0; i--)
	{
		if (claimed_player_starts[i].get_validated_object() == nullptr)
		{
			claimed_player_starts.remove_at(i);
		}
	}

	for (int i = 0; i < candidates.size(); i++)
	{
		Node* candidate = Object::cast_to<Node>(candidates[i]);
		if (candidate == nullptr || claimed_player_starts.has(candidate)) { continue; }

		claimed_player_starts.append(candidate);
		return candidate;
	}

	WARN_PRINT("GFGD: more players than player starts in this level, so starts are being reused.");

	const int player_id = controller != nullptr ? Math::max(controller->get_player_id(), 0) : 0;
	return Object::cast_to<Node>(candidates[player_id % candidates.size()]);
}

void GameModeBase::release_player_start(Controller* controller)
{
	// Starts are claimed per spawn rather than per controller, so a restart frees
	// whatever this controller was standing on by clearing dead entries.
	for (int i = claimed_player_starts.size() - 1; i >= 0; i--)
	{
		if (claimed_player_starts[i].get_validated_object() == nullptr)
		{
			claimed_player_starts.remove_at(i);
		}
	}
}

Variant GameModeBase::start_transform_for(Node* start_spot) const
{
	if (Node3D* start_3d = Object::cast_to<Node3D>(start_spot))
	{
		return start_3d->is_inside_tree() ? start_3d->get_global_transform() : start_3d->get_transform();
	}

	if (Node2D* start_2d = Object::cast_to<Node2D>(start_spot))
	{
		return start_2d->is_inside_tree() ? start_2d->get_global_transform() : start_2d->get_transform();
	}

	return Variant();
}

void GameModeBase::restart_player(Controller* controller)
{
	restart_player_at(controller, choose_player_start(controller));
}

void GameModeBase::restart_player_at(Controller* controller, Node* start_spot)
{
	if (controller == nullptr || world == nullptr) { return; }

	if (!world->has_authority())
	{
		WARN_PRINT("GFGD: spawning a pawn is the server's call.");
		return;
	}

	NetDriver* net_driver = world->get_net_driver();
	if (net_driver == nullptr) { return; }

	// Every controller the framework spawns a pawn for needs an id, because that
	// is what names the pawn on every peer. A controller a level placed by hand
	// gets one here.
	if (controller->get_player_id() == 0)
	{
		controller->set_player_id(world->take_next_player_id());
	}

	const int player_id = controller->get_player_id();

	net_driver->despawn_pawn(player_id);

	Ref<PackedScene> pawn_scene = get_pawn_scene_for(controller);
	if (pawn_scene.is_null())
	{
		WARN_PRINT("GFGD: this game mode has no default_pawn_scene, so no pawn was spawned.");
		emit_signal("player_restarted", Object::cast_to<PlayerController>(controller), (Object*)nullptr);
		return;
	}

	Node* pawn_root = net_driver->spawn_pawn(player_id, pawn_scene, start_transform_for(start_spot));
	Pawn* pawn = Pawn::find_in(pawn_root);

	emit_signal("player_restarted", Object::cast_to<PlayerController>(controller), pawn);
}

// --- Queries -----------------------------------------------------------------

int GameModeBase::get_num_players() const
{
	if (world == nullptr) { return 0; }

	GameStateBase* game_state = world->get_game_state();
	return game_state != nullptr ? game_state->get_player_count() : 0;
}

Array GameModeBase::get_player_controllers() const
{
	return world != nullptr ? world->get_player_controllers() : Array();
}

PlayerController* GameModeBase::get_player_controller_at(int index) const
{
	return world != nullptr ? world->get_player_controller_at(index) : nullptr;
}

void GameModeBase::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_world"), &GameModeBase::get_world);
	ClassDB::bind_method(D_METHOD("start_play"), &GameModeBase::start_play);

	ClassDB::bind_method(D_METHOD("login_local_player", "local_player"), &GameModeBase::login_local_player);
	ClassDB::bind_method(D_METHOD("login_peer", "peer_id"), &GameModeBase::login_peer);
	ClassDB::bind_method(D_METHOD("logout", "player_controller"), &GameModeBase::logout);
	ClassDB::bind_method(D_METHOD("logout_peer", "peer_id"), &GameModeBase::logout_peer);
	ClassDB::bind_method(D_METHOD("spawn_default_player"), &GameModeBase::spawn_default_player);
	ClassDB::bind_method(D_METHOD("restart_all_players"), &GameModeBase::restart_all_players);
	ClassDB::bind_method(D_METHOD("restart_player", "controller"), &GameModeBase::restart_player);
	ClassDB::bind_method(D_METHOD("restart_player_at", "controller", "start_spot"), &GameModeBase::restart_player_at);
	ClassDB::bind_method(D_METHOD("choose_player_start", "controller"), &GameModeBase::choose_player_start);
	ClassDB::bind_method(D_METHOD("find_player_start", "player_start_tag"), &GameModeBase::find_player_start);
	ClassDB::bind_method(D_METHOD("try_join", "device_slot"), &GameModeBase::try_join);

	ClassDB::bind_method(D_METHOD("get_num_players"), &GameModeBase::get_num_players);
	ClassDB::bind_method(D_METHOD("get_player_controllers"), &GameModeBase::get_player_controllers);
	ClassDB::bind_method(D_METHOD("get_player_controller_at", "index"), &GameModeBase::get_player_controller_at);
	ClassDB::bind_method(D_METHOD("get_pawn_scene_for_player", "player_index"), &GameModeBase::get_pawn_scene_for_player);
	ClassDB::bind_method(D_METHOD("get_pawn_scene_for", "controller"), &GameModeBase::get_pawn_scene_for);

	ClassDB::bind_method(D_METHOD("get_game_state_scene"), &GameModeBase::get_game_state_scene);
	ClassDB::bind_method(D_METHOD("set_game_state_scene", "value"), &GameModeBase::set_game_state_scene);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_state_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_game_state_scene", "get_game_state_scene");

	ClassDB::bind_method(D_METHOD("get_player_state_scene"), &GameModeBase::get_player_state_scene);
	ClassDB::bind_method(D_METHOD("set_player_state_scene", "value"), &GameModeBase::set_player_state_scene);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_state_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_player_state_scene", "get_player_state_scene");

	ClassDB::bind_method(D_METHOD("get_player_controller_scene"), &GameModeBase::get_player_controller_scene);
	ClassDB::bind_method(D_METHOD("set_player_controller_scene", "value"), &GameModeBase::set_player_controller_scene);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_controller_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_player_controller_scene", "get_player_controller_scene");

	ClassDB::bind_method(D_METHOD("get_default_pawn_scene"), &GameModeBase::get_default_pawn_scene);
	ClassDB::bind_method(D_METHOD("set_default_pawn_scene", "value"), &GameModeBase::set_default_pawn_scene);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "default_pawn_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_default_pawn_scene", "get_default_pawn_scene");

	ClassDB::bind_method(D_METHOD("get_pawn_scene_overrides"), &GameModeBase::get_pawn_scene_overrides);
	ClassDB::bind_method(D_METHOD("set_pawn_scene_overrides", "value"), &GameModeBase::set_pawn_scene_overrides);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "pawn_scene_overrides", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:PackedScene", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_pawn_scene_overrides", "get_pawn_scene_overrides");

	ClassDB::bind_method(D_METHOD("get_max_local_players"), &GameModeBase::get_max_local_players);
	ClassDB::bind_method(D_METHOD("set_max_local_players", "value"), &GameModeBase::set_max_local_players);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_local_players"), "set_max_local_players", "get_max_local_players");

	ClassDB::bind_method(D_METHOD("get_allow_press_to_join"), &GameModeBase::get_allow_press_to_join);
	ClassDB::bind_method(D_METHOD("set_allow_press_to_join", "value"), &GameModeBase::set_allow_press_to_join);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_press_to_join"), "set_allow_press_to_join", "get_allow_press_to_join");

	ClassDB::bind_method(D_METHOD("get_max_players"), &GameModeBase::get_max_players);
	ClassDB::bind_method(D_METHOD("set_max_players", "value"), &GameModeBase::set_max_players);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_players"), "set_max_players", "get_max_players");

	GDVIRTUAL_BIND(_init_game, "world");
	GDVIRTUAL_BIND(_init_game_state);
	GDVIRTUAL_BIND(_pre_login, "peer_id");
	GDVIRTUAL_BIND(_choose_player_start, "controller");
	GDVIRTUAL_BIND(_get_pawn_scene_for, "controller");
	GDVIRTUAL_BIND(_can_join, "device_slot");
	GDVIRTUAL_BIND(_on_post_login, "player_controller");
	GDVIRTUAL_BIND(_on_logout, "player_controller");

	ADD_SIGNAL(MethodInfo("play_started"));
	ADD_SIGNAL(MethodInfo("player_restarted",
			PropertyInfo(Variant::OBJECT, "player_controller", PROPERTY_HINT_NODE_TYPE, "PlayerController"),
			PropertyInfo(Variant::OBJECT, "pawn", PROPERTY_HINT_NODE_TYPE, "Pawn")));
	ADD_SIGNAL(MethodInfo("player_logged_in", PropertyInfo(Variant::OBJECT, "player_controller", PROPERTY_HINT_NODE_TYPE, "PlayerController")));
	ADD_SIGNAL(MethodInfo("player_logged_out", PropertyInfo(Variant::OBJECT, "player_controller", PROPERTY_HINT_NODE_TYPE, "PlayerController")));
	ADD_SIGNAL(MethodInfo("login_refused", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::STRING, "reason")));
}
}
