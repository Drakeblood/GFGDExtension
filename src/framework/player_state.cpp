#include "framework/player_state.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/net_replication.h"
#include "framework/delta_replicator.h"
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

	// Framework values are push-based: their setters mark them, nothing compares
	// them each frame. Script values are compared (or marked, with "|push").
	PackedStringArray own;
	own.push_back("player_name");
	own.push_back("player_id");
	own.push_back("player_index");
	own.push_back("unique_id");
	own.push_back("score");
	own.push_back("spectator");
	own.push_back("a_bot");
	own.push_back("ping");

	PackedStringArray script_properties;
	GDVIRTUAL_CALL(_get_replicated_properties, script_properties);
	PackedStringArray delta_properties;
	GDVIRTUAL_CALL(_get_delta_replicated_properties, delta_properties);
	Replication::attach_framework(this, own, script_properties, delta_properties);
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
	mark_delta_dirty("player_name");
	emit_signal("player_name_changed", player_name);
}

void PlayerState::set_score(int value)
{
	if (score == value) { return; }

	score = value;
	mark_delta_dirty("score");
	emit_signal("score_changed", score);
}

void PlayerState::set_player_id(int value)
{
	if (player_id == value) { return; }
	player_id = value;
	mark_delta_dirty("player_id");
}

void PlayerState::set_player_index(int value)
{
	if (player_index == value) { return; }
	player_index = value;
	mark_delta_dirty("player_index");
}

void PlayerState::set_unique_id(int value)
{
	if (unique_id == value) { return; }
	unique_id = value;
	mark_delta_dirty("unique_id");
}

void PlayerState::set_spectator(bool value)
{
	if (spectator == value) { return; }
	spectator = value;
	mark_delta_dirty("spectator");
}

void PlayerState::set_a_bot(bool value)
{
	if (a_bot == value) { return; }
	a_bot = value;
	mark_delta_dirty("a_bot");
}

void PlayerState::set_ping(float value)
{
	if (ping == value) { return; }
	ping = value;
	mark_delta_dirty("ping");
}

void PlayerState::mark_delta_dirty(const StringName& property, const Variant& key)
{
	DeltaReplicator* replicator = Object::cast_to<DeltaReplicator>(get_node_or_null(NodePath("Replication")));
	if (replicator == nullptr) { return; }
	if (key.get_type() == Variant::NIL) { replicator->mark_property_dirty(property); }
	else { replicator->mark_element_dirty(property, key); }
}

void PlayerState::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("mark_delta_dirty", "property", "key"), &PlayerState::mark_delta_dirty, DEFVAL(Variant()));
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
	GDVIRTUAL_BIND(_get_delta_replicated_properties);

	ADD_SIGNAL(MethodInfo("player_name_changed", PropertyInfo(Variant::STRING, "player_name")));
	ADD_SIGNAL(MethodInfo("score_changed", PropertyInfo(Variant::INT, "score")));
}
}
