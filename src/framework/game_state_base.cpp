#include "framework/game_state_base.h"
#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/e_net_packet_peer.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/net_replication.h"
#include "framework/player_state.h"

using namespace godot;

namespace GFGD
{
namespace
{
double local_seconds()
{
	return (double)Time::get_singleton()->get_ticks_msec() / 1000.0;
}
}

GameStateBase::GameStateBase()
{
	world = nullptr;
	has_begun_play = false;
	server_world_time = 0.0;
	server_world_time_received_at = 0.0;
	last_seen_server_world_time = 0.0;
	server_world_time_update_interval = 1.0f;
	time_since_time_update = 0.0f;
	time_since_ping_update = 0.0f;
}

GameStateBase::~GameStateBase()
{

}

void GameStateBase::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	PackedStringArray properties;
	properties.push_back("has_begun_play");
	properties.push_back("server_world_time");

	PackedStringArray script_properties;
	if (GDVIRTUAL_CALL(_get_replicated_properties, script_properties))
	{
		properties.append_array(script_properties);
	}

	Replication::attach(this, this, Replication::validate_properties(this, properties), World::SERVER_PEER_ID, "Replication");

	set_process(true);
}

void GameStateBase::_process(double delta)
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	update_server_world_time(delta);

	if (!has_authority()) { return; }

	time_since_ping_update += (float)delta;
	if (time_since_ping_update >= 1.0f)
	{
		time_since_ping_update = 0.0f;
		update_pings();
	}
}

void GameStateBase::update_server_world_time(double delta)
{
	if (has_authority())
	{
		time_since_time_update += (float)delta;
		if (time_since_time_update >= server_world_time_update_interval)
		{
			time_since_time_update = 0.0f;
			server_world_time = local_seconds();
		}
		return;
	}

	// A client only hears the clock now and then, so it notes when each reading
	// arrived and adds its own elapsed time on top until the next one.
	if (server_world_time != last_seen_server_world_time)
	{
		last_seen_server_world_time = server_world_time;
		server_world_time_received_at = local_seconds();
	}
}

double GameStateBase::get_server_world_time_seconds() const
{
	if (has_authority())
	{
		return local_seconds();
	}

	return server_world_time + (local_seconds() - server_world_time_received_at);
}

void GameStateBase::update_pings()
{
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	if (multiplayer.is_null() || !multiplayer->has_multiplayer_peer()) { return; }

	Ref<ENetMultiplayerPeer> enet_peer = multiplayer->get_multiplayer_peer();
	if (enet_peer.is_null()) { return; }

	for (int i = 0; i < player_array.size(); i++)
	{
		PlayerState* player_state = Object::cast_to<PlayerState>(player_array[i].get_validated_object());
		if (player_state == nullptr) { continue; }
		if (player_state->get_unique_id() == World::SERVER_PEER_ID) { continue; }

		Ref<ENetPacketPeer> packet_peer = enet_peer->get_peer(player_state->get_unique_id());
		if (packet_peer.is_null()) { continue; }

		player_state->set_ping((float)(packet_peer->get_statistic(ENetPacketPeer::PEER_ROUND_TRIP_TIME) / 1000.0));
	}
}

bool GameStateBase::has_authority() const
{
	return world == nullptr || world->has_authority();
}

void GameStateBase::init_game_state(World* in_world)
{
	world = in_world;
	GDVIRTUAL_CALL(_init_game_state, in_world);
}

void GameStateBase::handle_begin_play()
{
	if (!has_authority()) { return; }

	has_begun_play = true;
	emit_signal("begun_play");
}

void GameStateBase::add_player_state(PlayerState* player_state)
{
	if (player_state == nullptr) { return; }
	if (player_array.has(player_state)) { return; }

	if (player_state->get_parent() != this)
	{
		if (player_state->get_parent() != nullptr)
		{
			player_state->get_parent()->remove_child(player_state);
		}
		add_child(player_state);
	}

	player_array.append(player_state);
	emit_signal("player_state_added", player_state);
}

void GameStateBase::remove_player_state(PlayerState* player_state)
{
	if (player_state == nullptr) { return; }

	const int index = player_array.find(player_state);
	if (index < 0) { return; }

	player_array.remove_at(index);
	emit_signal("player_state_removed", player_state);

	if (player_state->get_parent() == this)
	{
		remove_child(player_state);
		player_state->queue_free();
	}
}

PlayerState* GameStateBase::get_player_state_by_player_id(int player_id) const
{
	for (int i = 0; i < player_array.size(); i++)
	{
		PlayerState* player_state = Object::cast_to<PlayerState>(player_array[i].get_validated_object());
		if (player_state != nullptr && player_state->get_player_id() == player_id)
		{
			return player_state;
		}
	}

	return nullptr;
}

PlayerState* GameStateBase::get_player_state_by_index(int player_index) const
{
	for (int i = 0; i < player_array.size(); i++)
	{
		PlayerState* player_state = Object::cast_to<PlayerState>(player_array[i].get_validated_object());
		if (player_state != nullptr && player_state->get_player_index() == player_index)
		{
			return player_state;
		}
	}

	return nullptr;
}

PlayerState* GameStateBase::get_player_state_by_unique_id(int unique_id) const
{
	for (int i = 0; i < player_array.size(); i++)
	{
		PlayerState* player_state = Object::cast_to<PlayerState>(player_array[i].get_validated_object());
		if (player_state != nullptr && player_state->get_unique_id() == unique_id)
		{
			return player_state;
		}
	}

	return nullptr;
}

void GameStateBase::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("add_player_state", "player_state"), &GameStateBase::add_player_state);
	ClassDB::bind_method(D_METHOD("remove_player_state", "player_state"), &GameStateBase::remove_player_state);
	ClassDB::bind_method(D_METHOD("get_player_array"), &GameStateBase::get_player_array);
	ClassDB::bind_method(D_METHOD("get_player_count"), &GameStateBase::get_player_count);
	ClassDB::bind_method(D_METHOD("get_player_state_by_player_id", "player_id"), &GameStateBase::get_player_state_by_player_id);
	ClassDB::bind_method(D_METHOD("get_player_state_by_index", "player_index"), &GameStateBase::get_player_state_by_index);
	ClassDB::bind_method(D_METHOD("get_player_state_by_unique_id", "unique_id"), &GameStateBase::get_player_state_by_unique_id);
	ClassDB::bind_method(D_METHOD("get_world"), &GameStateBase::get_world);
	ClassDB::bind_method(D_METHOD("has_authority"), &GameStateBase::has_authority);
	ClassDB::bind_method(D_METHOD("handle_begin_play"), &GameStateBase::handle_begin_play);

	ClassDB::bind_method(D_METHOD("get_has_begun_play"), &GameStateBase::get_has_begun_play);
	ClassDB::bind_method(D_METHOD("set_has_begun_play", "value"), &GameStateBase::set_has_begun_play);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_begun_play"), "set_has_begun_play", "get_has_begun_play");

	ClassDB::bind_method(D_METHOD("get_server_world_time"), &GameStateBase::get_server_world_time);
	ClassDB::bind_method(D_METHOD("set_server_world_time", "value"), &GameStateBase::set_server_world_time);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "server_world_time"), "set_server_world_time", "get_server_world_time");

	ClassDB::bind_method(D_METHOD("get_server_world_time_seconds"), &GameStateBase::get_server_world_time_seconds);

	GDVIRTUAL_BIND(_init_game_state, "world");
	GDVIRTUAL_BIND(_get_replicated_properties);

	ADD_SIGNAL(MethodInfo("player_state_added", PropertyInfo(Variant::OBJECT, "player_state", PROPERTY_HINT_NODE_TYPE, "PlayerState")));
	ADD_SIGNAL(MethodInfo("player_state_removed", PropertyInfo(Variant::OBJECT, "player_state", PROPERTY_HINT_NODE_TYPE, "PlayerState")));
	ADD_SIGNAL(MethodInfo("begun_play"));
}
}
