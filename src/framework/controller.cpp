#include "framework/controller.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/pawn.h"
#include "framework/player_state.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
Controller::Controller()
{
	pawn = nullptr;
	player_state = nullptr;
	player_id = 0;
}

Controller::~Controller()
{

}

World* Controller::get_world() const
{
	if (!is_inside_tree()) { return nullptr; }

	return Object::cast_to<World>(get_tree());
}

Node* Controller::get_pawn_root() const
{
	return pawn != nullptr ? pawn->get_pawn_root() : nullptr;
}

void Controller::set_player_state(PlayerState* value)
{
	if (player_state == value) { return; }

	player_state = value;
	emit_signal("player_state_changed", player_state);
}

void Controller::_enter_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// Null when the project did not set run/main_loop_type to World. The
	// controller still works as a plain node; it just is not in any world list.
	if (World* world = get_world())
	{
		world->register_controller(this);
	}
}

void Controller::_exit_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	if (World* world = get_world())
	{
		world->unregister_controller(this);
	}
}

void Controller::possess(Pawn* in_pawn)
{
	if (in_pawn == pawn) { return; }

	if (pawn != nullptr)
	{
		unpossess();
	}
	if (in_pawn == nullptr) { return; }

	if (in_pawn->get_controller() != nullptr)
	{
		in_pawn->get_controller()->unpossess();
	}

	pawn = in_pawn;
	in_pawn->possessed_by(this);

	on_possess(in_pawn);
	GDVIRTUAL_CALL(_on_possess, in_pawn);
	emit_signal("pawn_changed", in_pawn);
}

void Controller::unpossess()
{
	if (pawn == nullptr) { return; }

	Pawn* old_pawn = pawn;

	on_unpossess();
	GDVIRTUAL_CALL(_on_unpossess);

	pawn = nullptr;
	old_pawn->unpossessed();

	emit_signal("pawn_changed", (Object*)nullptr);
}

bool Controller::has_authority() const
{
	World* world = get_world();
	return world == nullptr || world->has_authority();
}

int Controller::get_local_role() const
{
	World* world = get_world();
	return world != nullptr ? (int)world->get_local_role_for(const_cast<Controller*>(this)) : (int)World::ROLE_AUTHORITY;
}

int Controller::get_remote_role() const
{
	World* world = get_world();
	return world != nullptr ? (int)world->get_remote_role_for(const_cast<Controller*>(this)) : (int)World::ROLE_NONE;
}

int Controller::get_owner_peer_id() const
{
	return World::SERVER_PEER_ID;
}

bool Controller::is_local_controller() const
{
	// A controller nobody connected to is the server's, so it is local wherever
	// the server is.
	return has_authority();
}

void Controller::on_possess(Pawn* in_pawn)
{

}

void Controller::on_unpossess()
{

}

void Controller::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("possess", "pawn"), &Controller::possess);
	ClassDB::bind_method(D_METHOD("unpossess"), &Controller::unpossess);
	ClassDB::bind_method(D_METHOD("get_pawn"), &Controller::get_pawn);
	ClassDB::bind_method(D_METHOD("get_pawn_root"), &Controller::get_pawn_root);
	ClassDB::bind_method(D_METHOD("get_world"), &Controller::get_world);

	ClassDB::bind_method(D_METHOD("get_player_state"), &Controller::get_player_state);
	ClassDB::bind_method(D_METHOD("set_player_state", "value"), &Controller::set_player_state);
	ClassDB::bind_method(D_METHOD("get_player_id"), &Controller::get_player_id);
	ClassDB::bind_method(D_METHOD("set_player_id", "value"), &Controller::set_player_id);

	ClassDB::bind_method(D_METHOD("has_authority"), &Controller::has_authority);
	ClassDB::bind_method(D_METHOD("get_local_role"), &Controller::get_local_role);
	ClassDB::bind_method(D_METHOD("get_remote_role"), &Controller::get_remote_role);
	ClassDB::bind_method(D_METHOD("get_owner_peer_id"), &Controller::get_owner_peer_id);
	ClassDB::bind_method(D_METHOD("is_player_controller"), &Controller::is_player_controller);
	ClassDB::bind_method(D_METHOD("is_local_controller"), &Controller::is_local_controller);

	GDVIRTUAL_BIND(_on_possess, "pawn");
	GDVIRTUAL_BIND(_on_unpossess);

	ADD_SIGNAL(MethodInfo("pawn_changed", PropertyInfo(Variant::OBJECT, "pawn", PROPERTY_HINT_NODE_TYPE, "Pawn")));
	ADD_SIGNAL(MethodInfo("player_state_changed", PropertyInfo(Variant::OBJECT, "player_state", PROPERTY_HINT_NODE_TYPE, "PlayerState")));
}
}
