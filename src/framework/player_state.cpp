#include "framework/player_state.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
PlayerState::PlayerState()
{
	player_index = 0;
	unique_id = 1;
	local = true;
	ping = 0.0f;
}

PlayerState::~PlayerState()
{

}

void PlayerState::set_player_name(const String& value)
{
	if (player_name == value) { return; }

	player_name = value;
	emit_signal("player_name_changed", player_name);
}

void PlayerState::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_player_name"), &PlayerState::get_player_name);
	ClassDB::bind_method(D_METHOD("set_player_name", "value"), &PlayerState::set_player_name);
	ClassDB::bind_method(D_METHOD("get_player_index"), &PlayerState::get_player_index);
	ClassDB::bind_method(D_METHOD("set_player_index", "value"), &PlayerState::set_player_index);
	ClassDB::bind_method(D_METHOD("get_unique_id"), &PlayerState::get_unique_id);
	ClassDB::bind_method(D_METHOD("set_unique_id", "value"), &PlayerState::set_unique_id);
	ClassDB::bind_method(D_METHOD("is_local"), &PlayerState::is_local);
	ClassDB::bind_method(D_METHOD("set_local", "value"), &PlayerState::set_local);
	ClassDB::bind_method(D_METHOD("get_ping"), &PlayerState::get_ping);
	ClassDB::bind_method(D_METHOD("set_ping", "value"), &PlayerState::set_ping);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_name"), "set_player_name", "get_player_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "player_index"), "set_player_index", "get_player_index");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "unique_id"), "set_unique_id", "get_unique_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "local"), "set_local", "is_local");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ping"), "set_ping", "get_ping");

	ADD_SIGNAL(MethodInfo("player_name_changed", PropertyInfo(Variant::STRING, "player_name")));
}
}
