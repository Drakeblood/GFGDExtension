#include "framework/player_state.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/net_replication.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
PlayerState::PlayerState()
{
	player_id = 0;
	player_index = 0;
	unique_id = World::SERVER_PEER_ID;
	local = true;
	score = 0;
	spectator = false;
	a_bot = false;
	ping = 0.0f;
}

PlayerState::~PlayerState()
{

}

void PlayerState::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	PackedStringArray properties;
	properties.push_back("player_name");
	properties.push_back("player_id");
	properties.push_back("player_index");
	properties.push_back("unique_id");
	properties.push_back("score");
	properties.push_back("spectator");
	properties.push_back("a_bot");
	properties.push_back("ping");

	PackedStringArray script_properties;
	if (GDVIRTUAL_CALL(_get_replicated_properties, script_properties))
	{
		properties.append_array(script_properties);
	}

	Replication::attach(this, this, Replication::validate_properties(this, properties), World::SERVER_PEER_ID, "Replication");
}

World* PlayerState::get_world() const
{
	if (!is_inside_tree()) { return nullptr; }

	return Object::cast_to<World>(get_tree());
}

bool PlayerState::has_authority() const
{
	World* world = get_world();
	return world == nullptr || world->has_authority();
}

int PlayerState::get_local_role() const
{
	World* world = get_world();
	return world != nullptr ? (int)world->get_local_role_for(const_cast<PlayerState*>(this)) : (int)World::ROLE_AUTHORITY;
}

int PlayerState::get_remote_role() const
{
	World* world = get_world();
	return world != nullptr ? (int)world->get_remote_role_for(const_cast<PlayerState*>(this)) : (int)World::ROLE_NONE;
}

void PlayerState::set_player_name(const String& value)
{
	if (player_name == value) { return; }

	player_name = value;
	emit_signal("player_name_changed", player_name);
}

void PlayerState::set_score(int value)
{
	if (score == value) { return; }

	score = value;
	emit_signal("score_changed", score);
}

void PlayerState::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_player_name"), &PlayerState::get_player_name);
	ClassDB::bind_method(D_METHOD("set_player_name", "value"), &PlayerState::set_player_name);
	ClassDB::bind_method(D_METHOD("get_player_id"), &PlayerState::get_player_id);
	ClassDB::bind_method(D_METHOD("set_player_id", "value"), &PlayerState::set_player_id);
	ClassDB::bind_method(D_METHOD("get_player_index"), &PlayerState::get_player_index);
	ClassDB::bind_method(D_METHOD("set_player_index", "value"), &PlayerState::set_player_index);
	ClassDB::bind_method(D_METHOD("get_unique_id"), &PlayerState::get_unique_id);
	ClassDB::bind_method(D_METHOD("set_unique_id", "value"), &PlayerState::set_unique_id);
	ClassDB::bind_method(D_METHOD("is_local"), &PlayerState::is_local);
	ClassDB::bind_method(D_METHOD("set_local", "value"), &PlayerState::set_local);
	ClassDB::bind_method(D_METHOD("get_score"), &PlayerState::get_score);
	ClassDB::bind_method(D_METHOD("set_score", "value"), &PlayerState::set_score);
	ClassDB::bind_method(D_METHOD("is_spectator"), &PlayerState::is_spectator);
	ClassDB::bind_method(D_METHOD("set_spectator", "value"), &PlayerState::set_spectator);
	ClassDB::bind_method(D_METHOD("is_a_bot"), &PlayerState::is_a_bot);
	ClassDB::bind_method(D_METHOD("set_a_bot", "value"), &PlayerState::set_a_bot);
	ClassDB::bind_method(D_METHOD("get_ping"), &PlayerState::get_ping);
	ClassDB::bind_method(D_METHOD("set_ping", "value"), &PlayerState::set_ping);

	ClassDB::bind_method(D_METHOD("get_world"), &PlayerState::get_world);
	ClassDB::bind_method(D_METHOD("has_authority"), &PlayerState::has_authority);
	ClassDB::bind_method(D_METHOD("get_local_role"), &PlayerState::get_local_role);
	ClassDB::bind_method(D_METHOD("get_remote_role"), &PlayerState::get_remote_role);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_name"), "set_player_name", "get_player_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "player_id"), "set_player_id", "get_player_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "player_index"), "set_player_index", "get_player_index");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "unique_id"), "set_unique_id", "get_unique_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "local"), "set_local", "is_local");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "score"), "set_score", "get_score");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "spectator"), "set_spectator", "is_spectator");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "a_bot"), "set_a_bot", "is_a_bot");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ping"), "set_ping", "get_ping");

	GDVIRTUAL_BIND(_get_replicated_properties);

	ADD_SIGNAL(MethodInfo("player_name_changed", PropertyInfo(Variant::STRING, "player_name")));
	ADD_SIGNAL(MethodInfo("score_changed", PropertyInfo(Variant::INT, "score")));
}
}
