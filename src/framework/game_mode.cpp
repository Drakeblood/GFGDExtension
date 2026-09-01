#include "framework/game_mode.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/window.hpp>

#include "framework/gfgd_scene_tree.h"
#include "framework/game_instance.h"
#include "framework/game_mode_settings.h"
#include "framework/game_state.h"
#include "framework/level.h"
#include "framework/local_player.h"
#include "framework/player_controller.h"
#include "framework/player_input.h"
#include "framework/player_state.h"
#include "framework/pawn_handler.h"
#include "core/gfgd_statics.h"

using namespace godot;

namespace GFGD
{
GameMode::GameMode()
{
	scene_tree = nullptr;
}

GameMode::~GameMode()
{

}

void GameMode::init_game(GFGDSceneTree* in_scene_tree)
{
	scene_tree = in_scene_tree;

	bool handled = false;
	GDVIRTUAL_CALL(_init_game, in_scene_tree, handled);
	if (!handled)
	{
		restart_all_players();
	}
}

namespace
{
// Node3D/Node2D global transforms are only defined inside the scene tree, and the
// level is deliberately still outside it while the game mode spawns. Accumulate
// the local transforms up to the level instead: once the level is added, its own
// transform applies to the pawn and to the start node alike, so the two agree.
Transform3D transform_relative_to_3d(Node* node, Node* ancestor)
{
	Transform3D accumulated;
	for (Node* current = node; current != nullptr && current != ancestor; current = current->get_parent())
	{
		if (Node3D* current_3d = Object::cast_to<Node3D>(current))
		{
			accumulated = current_3d->get_transform() * accumulated;
		}
	}
	return accumulated;
}

Transform2D transform_relative_to_2d(Node* node, Node* ancestor)
{
	Transform2D accumulated;
	for (Node* current = node; current != nullptr && current != ancestor; current = current->get_parent())
	{
		if (Node2D* current_2d = Object::cast_to<Node2D>(current))
		{
			accumulated = current_2d->get_transform() * accumulated;
		}
	}
	return accumulated;
}
}

Node* GameMode::get_pawn_parent() const
{
	// This can run before the game mode is in the scene tree, so get_tree() is not
	// available - the tree to use is the one handed to init_game.
	if (scene_tree != nullptr)
	{
		return scene_tree->get_level() != nullptr
				? static_cast<Node*>(scene_tree->get_level())
				: static_cast<Node*>(scene_tree->get_root());
	}

	return is_inside_tree() ? get_tree()->get_root() : nullptr;
}

PlayerController* GameMode::create_player_controller()
{
	PlayerController* new_controller = nullptr;

	if (game_mode_settings.is_valid() && game_mode_settings->get_player_controller_scene().is_valid())
	{
		Node* controller_node = game_mode_settings->get_player_controller_scene()->instantiate();
		new_controller = Object::cast_to<PlayerController>(controller_node);
		if (new_controller == nullptr && controller_node != nullptr)
		{
			WARN_PRINT("GFGD: player_controller_scene root is not a PlayerController. Using a default PlayerController instead.");
			controller_node->queue_free();
		}
	}

	if (new_controller == nullptr)
	{
		new_controller = memnew(PlayerController);
	}

	return new_controller;
}

PlayerState* GameMode::create_player_state_for(PlayerController* in_player_controller)
{
	if (scene_tree == nullptr) { return nullptr; }

	GameState* game_state = scene_tree->get_game_state();
	if (game_state == nullptr)
	{
		WARN_PRINT("GFGD: No GameState exists, so no PlayerState was created.");
		return nullptr;
	}

	PlayerState* player_state = nullptr;
	if (game_mode_settings.is_valid() && game_mode_settings->get_player_state_scene().is_valid())
	{
		Node* player_state_node = game_mode_settings->get_player_state_scene()->instantiate();
		player_state = Object::cast_to<PlayerState>(player_state_node);
		if (player_state == nullptr && player_state_node != nullptr)
		{
			WARN_PRINT("GFGD: player_state_scene root is not a PlayerState. Using a default PlayerState instead.");
			player_state_node->queue_free();
		}
	}

	if (player_state == nullptr)
	{
		player_state = memnew(PlayerState);
	}

	const int player_index = in_player_controller->get_player_index();
	player_state->set_name(vformat("PlayerState%d", player_index));
	player_state->set_player_index(player_index);
	player_state->set_local(in_player_controller->is_local_player_controller());
	if (player_state->get_player_name().is_empty())
	{
		player_state->set_player_name(vformat("Player %d", player_index + 1));
	}

	game_state->add_player_state(player_state);
	in_player_controller->set_player_state(player_state);

	return player_state;
}

PlayerController* GameMode::login(LocalPlayer* local_player)
{
	if (local_player == nullptr)
	{
		ERR_PRINT("GFGD: GameMode::login was given no LocalPlayer.");
		return nullptr;
	}

	if (scene_tree != nullptr && !scene_tree->has_authority())
	{
		WARN_PRINT("GFGD: GameMode::login was called without authority. Player controllers are spawned by the server.");
		return nullptr;
	}

	if (local_player->get_player_controller() != nullptr)
	{
		return local_player->get_player_controller();
	}

	PlayerController* new_controller = create_player_controller();
	new_controller->set_name(vformat("PlayerController%d", local_player->get_player_index()));

	// The LocalPlayer must be attached before anything possesses or spawns: the
	// camera picks the player's viewport from it, and the input pump reads its
	// PlayerInput. Doing this after restart_player would silently fall back to
	// the global Input singleton.
	new_controller->set_local_player(local_player);

	add_child(new_controller);

	// The controller registers itself with the world in _enter_tree, but the game
	// mode is still outside the tree during _init_game. Register eagerly so a
	// script asking for the controller list from _init_game already sees it; both
	// paths are idempotent.
	if (scene_tree != nullptr)
	{
		scene_tree->register_controller(new_controller);
	}

	create_player_state_for(new_controller);

	restart_player(new_controller);

	GDVIRTUAL_CALL(_on_post_login, new_controller);
	emit_signal("player_logged_in", new_controller);

	return new_controller;
}

void GameMode::logout(PlayerController* in_player_controller)
{
	if (in_player_controller == nullptr) { return; }

	GDVIRTUAL_CALL(_on_logout, in_player_controller);
	emit_signal("player_logged_out", in_player_controller);

	release_player_start(in_player_controller);

	if (PawnHandler* pawn_handler = in_player_controller->get_pawn_handler())
	{
		Node* pawn_root = pawn_handler->get_pawn_root();
		in_player_controller->unpossess();
		if (pawn_root != nullptr)
		{
			pawn_root->queue_free();
		}
	}

	if (scene_tree != nullptr)
	{
		if (GameState* game_state = scene_tree->get_game_state())
		{
			game_state->remove_player_state(in_player_controller->get_player_state());
		}
	}
	in_player_controller->set_player_state(nullptr);
	in_player_controller->set_local_player(nullptr);

	if (in_player_controller->get_parent() != nullptr)
	{
		in_player_controller->get_parent()->remove_child(in_player_controller);
	}
	in_player_controller->queue_free();
}

void GameMode::restart_all_players()
{
	if (scene_tree == nullptr)
	{
		ERR_PRINT("GFGD: GameMode::restart_all_players has no scene tree. Call it from _init_game or later.");
		return;
	}

	GameInstance* game_instance = scene_tree->get_game_instance();
	if (game_instance == nullptr) { return; }

	const int max_local_players = game_mode_settings.is_valid() ? game_mode_settings->get_max_local_players() : 1;

	// A project that never touched the new API has no local player yet, so make
	// the one that has always existed implicitly.
	if (game_instance->get_local_player_count() == 0)
	{
		scene_tree->create_default_local_player();
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

		login(local_player);
	}
}

PlayerController* GameMode::spawn_default_player()
{
	if (scene_tree == nullptr)
	{
		ERR_PRINT("GFGD: spawn_default_player has no scene tree to spawn into. Call it from _init_game or later.");
		return nullptr;
	}

	GameInstance* game_instance = scene_tree->get_game_instance();
	if (game_instance == nullptr) { return nullptr; }

	if (game_instance->get_local_player_count() == 0)
	{
		scene_tree->create_default_local_player();
	}

	LocalPlayer* local_player = game_instance->get_local_player(0);
	if (local_player == nullptr) { return nullptr; }

	if (local_player->get_player_controller() != nullptr)
	{
		return local_player->get_player_controller();
	}

	return login(local_player);
}

bool GameMode::try_join(int device_slot)
{
	if (scene_tree == nullptr) { return false; }
	if (!game_mode_settings.is_valid() || !game_mode_settings->get_allow_press_to_join()) { return false; }

	GameInstance* game_instance = scene_tree->get_game_instance();
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

	if (waiting_player == nullptr && game_instance->get_local_player_count() >= game_mode_settings->get_max_local_players())
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
		login(local_player);
	}

	return true;
}

Node* GameMode::choose_player_start(Controller* controller)
{
	Node* chosen = nullptr;
	if (GDVIRTUAL_CALL(_choose_player_start, controller, chosen) && chosen != nullptr)
	{
		claimed_player_starts.append(chosen);
		return chosen;
	}

	Node* pawn_parent = get_pawn_parent();
	if (pawn_parent == nullptr) { return nullptr; }

	// A name glob rather than a class: it keeps working for the single node named
	// "PlayerStart" that projects already have, and picks up PlayerStart2,
	// PlayerStart_Blue and so on for free. owned = false matches the search this
	// replaces and finds starts added at runtime.
	TypedArray<Node> candidates = pawn_parent->find_children("PlayerStart*", "", true, false);
	if (candidates.is_empty()) { return nullptr; }

	for (int i = 0; i < candidates.size(); i++)
	{
		Node* candidate = Object::cast_to<Node>(candidates[i]);
		if (candidate == nullptr || claimed_player_starts.has(candidate)) { continue; }

		claimed_player_starts.append(candidate);
		return candidate;
	}

	// More players than starts. Physics overlap tests are not available here - the
	// level is deliberately still outside the tree - so fall back to sharing.
	WARN_PRINT("GFGD: More players than PlayerStart nodes in this level, so starts are being reused.");

	PlayerController* as_player = Object::cast_to<PlayerController>(controller);
	const int player_index = as_player != nullptr ? Math::max(as_player->get_player_index(), 0) : 0;
	return Object::cast_to<Node>(candidates[player_index % candidates.size()]);
}

void GameMode::release_player_start(Controller* controller)
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

Node* GameMode::spawn_default_pawn_for(Controller* controller)
{
	if (controller == nullptr) { return nullptr; }

	Node* pawn_parent = get_pawn_parent();
	if (pawn_parent == nullptr)
	{
		ERR_PRINT("GFGD: spawn_default_pawn_for has no scene tree to spawn into. Call it from _init_game or later.");
		return nullptr;
	}

	PlayerController* as_player = Object::cast_to<PlayerController>(controller);
	const int player_index = as_player != nullptr ? Math::max(as_player->get_player_index(), 0) : 0;

	Ref<PackedScene> pawn_scene;
	if (game_mode_settings.is_valid())
	{
		pawn_scene = game_mode_settings->get_pawn_scene_for_player(player_index);
	}

	if (pawn_scene.is_null())
	{
		WARN_PRINT("GFGD: GameModeSettings has no pawn_scene set. No pawn will be spawned.");
		return nullptr;
	}

	Node* pawn = pawn_scene->instantiate();
	if (pawn == nullptr) { return nullptr; }

	// Godot auto-renames the second instance of a scene to something like
	// @Node3D@2, which is useless in a log with more than one player. Only from
	// player 1 onward, so a single player project keeps the name it authored.
	if (player_index > 0)
	{
		pawn->set_name(vformat("%s%d", pawn->get_name(), player_index));
	}

	pawn_parent->add_child(pawn);

	if (Node* player_start = choose_player_start(controller))
	{
		Node3D* pawn_3d = Object::cast_to<Node3D>(pawn);
		Node3D* start_3d = Object::cast_to<Node3D>(player_start);
		Node2D* pawn_2d = Object::cast_to<Node2D>(pawn);
		Node2D* start_2d = Object::cast_to<Node2D>(player_start);
		if (pawn_3d != nullptr && start_3d != nullptr)
		{
			if (start_3d->is_inside_tree())
			{
				pawn_3d->set_global_transform(start_3d->get_global_transform());
			}
			else
			{
				pawn_3d->set_transform(transform_relative_to_3d(start_3d, pawn_parent));
			}
		}
		else if (pawn_2d != nullptr && start_2d != nullptr)
		{
			if (start_2d->is_inside_tree())
			{
				pawn_2d->set_global_transform(start_2d->get_global_transform());
			}
			else
			{
				pawn_2d->set_transform(transform_relative_to_2d(start_2d, pawn_parent));
			}
		}
	}

	return pawn;
}

void GameMode::restart_player(Controller* controller)
{
	if (controller == nullptr) { return; }

	Node* pawn = nullptr;
	GDVIRTUAL_CALL(_spawn_default_pawn_for, controller, pawn);
	if (pawn == nullptr)
	{
		pawn = spawn_default_pawn_for(controller);
	}

	PawnHandler* pawn_handler = nullptr;
	if (pawn != nullptr)
	{
		pawn_handler = Object::cast_to<PawnHandler>(pawn);
		if (pawn_handler == nullptr)
		{
			TypedArray<Node> handlers = pawn->find_children("*", "PawnHandler", true, false);
			if (handlers.size() > 0)
			{
				pawn_handler = Object::cast_to<PawnHandler>(handlers[0]);
			}
		}

		if (pawn_handler != nullptr)
		{
			controller->possess(pawn_handler);
		}
		else
		{
			WARN_PRINT("GFGD: pawn_scene does not contain a PawnHandler node. The pawn cannot be possessed.");
		}
	}

	PlayerController* as_player = Object::cast_to<PlayerController>(controller);
	emit_signal("player_spawned", as_player, pawn_handler);
	emit_signal("player_restarted", as_player, pawn_handler);
}

Array GameMode::get_player_controllers() const
{
	return scene_tree != nullptr ? scene_tree->get_player_controllers() : Array();
}

PlayerController* GameMode::get_player_controller_at(int index) const
{
	return scene_tree != nullptr ? scene_tree->get_player_controller_at(index) : nullptr;
}

void GameMode::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("spawn_default_player"), &GameMode::spawn_default_player);
	ClassDB::bind_method(D_METHOD("get_gfgd_scene_tree"), &GameMode::get_gfgd_scene_tree);

	ClassDB::bind_method(D_METHOD("login", "local_player"), &GameMode::login);
	ClassDB::bind_method(D_METHOD("logout", "player_controller"), &GameMode::logout);
	ClassDB::bind_method(D_METHOD("restart_all_players"), &GameMode::restart_all_players);
	ClassDB::bind_method(D_METHOD("restart_player", "controller"), &GameMode::restart_player);
	ClassDB::bind_method(D_METHOD("spawn_default_pawn_for", "controller"), &GameMode::spawn_default_pawn_for);
	ClassDB::bind_method(D_METHOD("choose_player_start", "controller"), &GameMode::choose_player_start);
	ClassDB::bind_method(D_METHOD("try_join", "device_slot"), &GameMode::try_join);
	ClassDB::bind_method(D_METHOD("get_player_controllers"), &GameMode::get_player_controllers);
	ClassDB::bind_method(D_METHOD("get_player_controller_at", "index"), &GameMode::get_player_controller_at);

	ClassDB::bind_method(D_METHOD("get_game_mode_settings"), &GameMode::get_game_mode_settings);
	ClassDB::bind_method(D_METHOD("set_game_mode_settings", "settings"), &GameMode::set_game_mode_settings);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_mode_settings", PROPERTY_HINT_RESOURCE_TYPE, "GameModeSettings"), "set_game_mode_settings", "get_game_mode_settings");

	GDVIRTUAL_BIND(_init_game, "scene_tree");
	GDVIRTUAL_BIND(_choose_player_start, "controller");
	GDVIRTUAL_BIND(_spawn_default_pawn_for, "controller");
	GDVIRTUAL_BIND(_can_join, "device_slot");
	GDVIRTUAL_BIND(_on_post_login, "player_controller");
	GDVIRTUAL_BIND(_on_logout, "player_controller");

	ADD_SIGNAL(MethodInfo("player_spawned",
			PropertyInfo(Variant::OBJECT, "player_controller", PROPERTY_HINT_NODE_TYPE, "PlayerController"),
			PropertyInfo(Variant::OBJECT, "pawn_handler", PROPERTY_HINT_NODE_TYPE, "PawnHandler")));
	ADD_SIGNAL(MethodInfo("player_restarted",
			PropertyInfo(Variant::OBJECT, "player_controller", PROPERTY_HINT_NODE_TYPE, "PlayerController"),
			PropertyInfo(Variant::OBJECT, "pawn_handler", PROPERTY_HINT_NODE_TYPE, "PawnHandler")));
	ADD_SIGNAL(MethodInfo("player_logged_in", PropertyInfo(Variant::OBJECT, "player_controller", PROPERTY_HINT_NODE_TYPE, "PlayerController")));
	ADD_SIGNAL(MethodInfo("player_logged_out", PropertyInfo(Variant::OBJECT, "player_controller", PROPERTY_HINT_NODE_TYPE, "PlayerController")));
}
}
