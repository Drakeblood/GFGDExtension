#include "debug/debug_replicator.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "debug/console_manager.h"
#include "debug/gameplay_debugger.h"
#include "framework/player_controller.h"
#include "framework/world.h"

using namespace GFGD;

DebugReplicator::DebugReplicator()
{
	view_active = false;
	collect_accumulator = 0.0;
	view_data_msec = 0;
}

DebugReplicator* DebugReplicator::find_for(PlayerController* controller)
{
	if (controller == nullptr) { return nullptr; }
	return Object::cast_to<DebugReplicator>(controller->get_node_or_null(NodePath("DebugReplicator")));
}

void DebugReplicator::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// Any peer may call any of these; each checks the sender itself, because
	// "the owning client" is not something the multiplayer API can express.
	Dictionary reliable;
	reliable["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	reliable["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
	reliable["call_local"] = false;
	reliable["channel"] = 0;
	rpc_config("server_exec", reliable);
	rpc_config("client_output", reliable);
	rpc_config("server_set_view", reliable);

	// A snapshot is usually bigger than one packet, and an unreliable message
	// that has to be split is lost whole if any piece is. Reliable, then, but on
	// a channel of its own so it never holds up the game's reliable traffic.
	Dictionary view = reliable.duplicate();
	view["channel"] = 3;
	rpc_config("client_receive_view", view);

	set_process(false);
}

PlayerController* DebugReplicator::get_player_controller() const
{
	return Object::cast_to<PlayerController>(get_parent());
}

bool DebugReplicator::is_server() const
{
	World* world = Object::cast_to<World>(get_tree());
	return world == nullptr || world->has_authority();
}

bool DebugReplicator::is_local_owner() const
{
	PlayerController* pc = get_player_controller();
	return pc != nullptr && pc->is_local_player_controller();
}

bool DebugReplicator::is_from_owner() const
{
	PlayerController* pc = get_player_controller();
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	if (pc == nullptr || multiplayer.is_null()) { return false; }
	return multiplayer->get_remote_sender_id() == pc->get_owner_peer_id();
}

bool DebugReplicator::is_from_server() const
{
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	return multiplayer.is_valid() && multiplayer->get_remote_sender_id() == World::SERVER_PEER_ID;
}

// --- Console ------------------------------------------------------------------

void DebugReplicator::request_exec(const String& line)
{
	if (is_server())
	{
		ConsoleManager::get_singleton()->execute_remote(line, get_player_controller());
		return;
	}
	rpc_id(World::SERVER_PEER_ID, "server_exec", line);
}

void DebugReplicator::server_exec(const String& line)
{
	if (!is_server() || !is_from_owner())
	{
		WARN_PRINT("GFGD: a console command arrived from a peer that does not own this controller; dropped.");
		return;
	}
	ConsoleManager::get_singleton()->execute_remote(line, get_player_controller());
}

void DebugReplicator::send_output(const String& text, int level)
{
	PlayerController* pc = get_player_controller();
	if (pc == nullptr) { return; }
	if (is_local_owner() || !is_server())
	{
		ConsoleManager::get_singleton()->emit_output(text, level);
		return;
	}
	rpc_id(pc->get_owner_peer_id(), "client_output", text, level);
}

void DebugReplicator::client_output(const String& text, int level)
{
	if (!is_from_server()) { return; }
	ConsoleManager::get_singleton()->emit_output(text, level);
}

// --- Gameplay debugger --------------------------------------------------------

void DebugReplicator::request_view(bool active, const NodePath& target, const PackedStringArray& categories)
{
	if (is_server())
	{
		apply_view(active, target, categories);
		return;
	}
	rpc_id(World::SERVER_PEER_ID, "server_set_view", active, target, categories);
}

void DebugReplicator::server_set_view(bool active, const NodePath& target, const PackedStringArray& categories)
{
	if (!is_server() || !is_from_owner()) { return; }
	apply_view(active, target, categories);
}

void DebugReplicator::apply_view(bool active, const NodePath& target, const PackedStringArray& categories)
{
	view_active = active;
	view_target = target;
	view_categories = categories;
	collect_accumulator = 0.0;
	set_process(active);
	if (active) { collect_and_send(); }
}

void DebugReplicator::_process(double delta)
{
	if (!view_active) { return; }

	collect_accumulator += delta;
	const double interval = ProjectSettings::get_singleton()->get_setting("application/game_framework/debug/gameplay_debugger_refresh_interval", 0.15);
	if (collect_accumulator < interval) { return; }
	collect_accumulator = 0.0;
	collect_and_send();
}

void DebugReplicator::collect_and_send()
{
	GameplayDebugger* debugger = GameplayDebugger::get_singleton();
	PlayerController* pc = get_player_controller();
	if (debugger == nullptr || pc == nullptr || !is_inside_tree()) { return; }

	Node* target = view_target.is_empty() ? nullptr : get_tree()->get_root()->get_node_or_null(view_target);
	const Dictionary data = debugger->collect(target, view_categories, pc);

	if (is_local_owner())
	{
		client_receive_view(view_target, data);
		return;
	}
	rpc_id(pc->get_owner_peer_id(), "client_receive_view", view_target, data);
}

void DebugReplicator::client_receive_view(const NodePath& target, const Dictionary& data)
{
	// Called directly for the server's own player, so only a remote call is checked.
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const bool remote_call = multiplayer.is_valid() && multiplayer->get_remote_sender_id() != 0;
	if (remote_call && !is_from_server()) { return; }

	view_data_target = target;
	view_data = data;
	view_data_msec = Time::get_singleton()->get_ticks_msec();
}

double DebugReplicator::get_view_data_age() const
{
	if (view_data_msec == 0) { return -1.0; }
	return (Time::get_singleton()->get_ticks_msec() - view_data_msec) / 1000.0;
}

void DebugReplicator::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_player_controller"), &DebugReplicator::get_player_controller);
	ClassDB::bind_method(D_METHOD("request_exec", "line"), &DebugReplicator::request_exec);
	ClassDB::bind_method(D_METHOD("send_output", "text", "level"), &DebugReplicator::send_output);
	ClassDB::bind_method(D_METHOD("server_exec", "line"), &DebugReplicator::server_exec);
	ClassDB::bind_method(D_METHOD("client_output", "text", "level"), &DebugReplicator::client_output);
	ClassDB::bind_method(D_METHOD("request_view", "active", "target", "categories"), &DebugReplicator::request_view);
	ClassDB::bind_method(D_METHOD("server_set_view", "active", "target", "categories"), &DebugReplicator::server_set_view);
	ClassDB::bind_method(D_METHOD("client_receive_view", "target", "data"), &DebugReplicator::client_receive_view);
	ClassDB::bind_method(D_METHOD("get_view_data"), &DebugReplicator::get_view_data);
	ClassDB::bind_method(D_METHOD("get_view_data_target"), &DebugReplicator::get_view_data_target);
	ClassDB::bind_method(D_METHOD("get_view_data_age"), &DebugReplicator::get_view_data_age);
}
