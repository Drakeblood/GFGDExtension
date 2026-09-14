#include "framework/pawn.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/controller.h"
#include "framework/input_component.h"
#include "framework/net_replication.h"
#include "framework/player_controller.h"
#include "framework/player_state.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
Pawn::Pawn()
{
	controller = nullptr;
	auto_manage_camera = true;
	auto_possess_player = AUTO_POSSESS_DISABLED;
	replicate_transform = true;
	movement_predicted = false;
	abilities_replicated = false;
	player_id = 0;
}

Pawn::~Pawn()
{

}

Pawn* Pawn::find_in(Node* pawn_root)
{
	if (pawn_root == nullptr) { return nullptr; }

	if (Pawn* as_pawn = Object::cast_to<Pawn>(pawn_root))
	{
		return as_pawn;
	}

	TypedArray<Node> found = pawn_root->find_children("*", "Pawn", true, false);
	return found.size() > 0 ? Object::cast_to<Pawn>(found[0]) : nullptr;
}

World* Pawn::get_world() const
{
	if (!is_inside_tree()) { return nullptr; }

	return Object::cast_to<World>(get_tree());
}

void Pawn::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	setup_replication();

	if (auto_possess_player != AUTO_POSSESS_DISABLED)
	{
		// Deferred, because a pawn placed in a level by hand enters the tree while
		// the game mode is still logging players in.
		callable_mp(this, &Pawn::apply_auto_possess).call_deferred();
	}
}

void Pawn::setup_replication()
{
	// Both sides build the same synchronizers under the same names, which is what
	// pairs them up: one for where the pawn's root is, one for whatever a script
	// on this node wants kept in step. Both hang here rather than off the root,
	// because a node may not be given a child while it is still starting up.
	if (replicate_transform && get_pawn_root() != nullptr)
	{
		// Through this node's replicated_* properties rather than the root's own,
		// so a client predicting the pawn can drop what arrives - decided when it
		// arrives, because the movement component that marks the pawn predicted
		// may be readied after this node, and ownership follows possession.
		PackedStringArray root_properties;
		root_properties.push_back("position");
		root_properties.push_back("rotation");

		// Checked against the root, so a root without them still gets the
		// warning it always did; each one that exists is mirrored through its
		// proxy on this node.
		const PackedStringArray present = Replication::validate_properties(get_pawn_root(), root_properties);
		PackedStringArray transform_properties;
		for (int i = 0; i < present.size(); i++)
		{
			transform_properties.push_back("replicated_" + present[i]);
		}

		Replication::attach(this, this, transform_properties, World::SERVER_PEER_ID, "TransformReplication");
	}

	PackedStringArray script_properties;
	if (GDVIRTUAL_CALL(_get_replicated_properties, script_properties) && !script_properties.is_empty())
	{
		Replication::attach(this, this, Replication::validate_properties(this, script_properties), World::SERVER_PEER_ID, "Replication");
	}
}

void Pawn::apply_auto_possess()
{
	if (controller != nullptr) { return; }

	World* world = get_world();
	if (world == nullptr) { return; }

	// Handing a pawn to a player is the server's decision, like every other
	// possession.
	if (!world->has_authority()) { return; }

	if (Controller* target = world->get_player_controller_at(auto_possess_player))
	{
		target->possess(this);
	}
}

void Pawn::possessed_by(Controller* new_controller)
{
	controller = new_controller;
	GDVIRTUAL_CALL(_possessed, new_controller);
	emit_signal("possessed", new_controller);
}

void Pawn::unpossessed()
{
	controller = nullptr;
	movement_input = Vector3();
	GDVIRTUAL_CALL(_unpossessed);
	emit_signal("unpossessed");
}

void Pawn::setup_input_component(InputComponent* input_component)
{
	GDVIRTUAL_CALL(_setup_input_component, input_component);
}

PlayerState* Pawn::get_player_state() const
{
	return controller != nullptr ? controller->get_player_state() : nullptr;
}

bool Pawn::is_player_controlled() const
{
	return controller != nullptr && controller->is_player_controller();
}

bool Pawn::is_bot_controlled() const
{
	return controller != nullptr && !controller->is_player_controller();
}

bool Pawn::is_locally_controlled() const
{
	return controller != nullptr && controller->is_local_controller();
}

bool Pawn::has_authority() const
{
	World* world = get_world();
	return world == nullptr || world->has_authority();
}

bool Pawn::wants_movement_input() const
{
	if (is_locally_predicted()) { return true; }
	if (!has_authority()) { return false; }
	if (!movement_predicted) { return true; }

	// The same test PredictedMovementBackend makes: a remote owner means the
	// moves come from that owner.
	World* world = get_world();
	if (world == nullptr || !world->is_networked()) { return true; }

	const int owner_peer = get_owner_peer_id();
	return owner_peer == World::SERVER_PEER_ID || owner_peer == world->get_local_peer_id();
}

bool Pawn::runs_input_locally() const
{
	return (movement_predicted || abilities_replicated) && !has_authority() && is_locally_controlled();
}

bool Pawn::is_locally_predicted() const
{
	return movement_predicted && !has_authority() && is_locally_controlled();
}

Variant Pawn::get_replicated_position() const
{
	const Node* root = get_pawn_root();
	return root != nullptr ? root->get("position") : Variant();
}

void Pawn::set_replicated_position(const Variant& value)
{
	Node* root = get_pawn_root();
	if (root == nullptr || is_locally_predicted()) { return; }

	root->set("position", value);
}

Variant Pawn::get_replicated_rotation() const
{
	const Node* root = get_pawn_root();
	return root != nullptr ? root->get("rotation") : Variant();
}

void Pawn::set_replicated_rotation(const Variant& value)
{
	Node* root = get_pawn_root();
	if (root == nullptr || is_locally_predicted()) { return; }

	root->set("rotation", value);
}

int Pawn::get_local_role() const
{
	World* world = get_world();
	return world != nullptr ? (int)world->get_local_role_for(const_cast<Pawn*>(this)) : (int)World::ROLE_AUTHORITY;
}

int Pawn::get_remote_role() const
{
	World* world = get_world();
	return world != nullptr ? (int)world->get_remote_role_for(const_cast<Pawn*>(this)) : (int)World::ROLE_NONE;
}

int Pawn::get_owner_peer_id() const
{
	World* world = get_world();
	return world != nullptr ? world->get_net_owner_peer(const_cast<Pawn*>(this)) : World::SERVER_PEER_ID;
}

void Pawn::gather_movement_input(double delta)
{
	GDVIRTUAL_CALL(_gather_movement_input, delta);
}

void Pawn::add_movement_input(const Vector3& world_direction, float scale)
{
	movement_input += world_direction * scale;
}

Vector3 Pawn::consume_movement_input_vector()
{
	const Vector3 consumed = movement_input;
	movement_input = Vector3();
	return consumed;
}

void Pawn::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_controller"), &Pawn::get_controller);
	ClassDB::bind_method(D_METHOD("get_pawn_root"), &Pawn::get_pawn_root);
	ClassDB::bind_method(D_METHOD("get_player_state"), &Pawn::get_player_state);
	ClassDB::bind_method(D_METHOD("setup_input_component", "input_component"), &Pawn::setup_input_component);
	ClassDB::bind_method(D_METHOD("get_world"), &Pawn::get_world);

	ClassDB::bind_method(D_METHOD("is_locally_controlled"), &Pawn::is_locally_controlled);
	ClassDB::bind_method(D_METHOD("is_player_controlled"), &Pawn::is_player_controlled);
	ClassDB::bind_method(D_METHOD("is_bot_controlled"), &Pawn::is_bot_controlled);
	ClassDB::bind_method(D_METHOD("has_authority"), &Pawn::has_authority);
	ClassDB::bind_method(D_METHOD("wants_movement_input"), &Pawn::wants_movement_input);
	ClassDB::bind_method(D_METHOD("is_locally_predicted"), &Pawn::is_locally_predicted);
	ClassDB::bind_method(D_METHOD("get_movement_predicted"), &Pawn::get_movement_predicted);
	ClassDB::bind_method(D_METHOD("get_abilities_replicated"), &Pawn::get_abilities_replicated);
	ClassDB::bind_method(D_METHOD("runs_input_locally"), &Pawn::runs_input_locally);

	// Not for scripts - they are what the transform replication reads and
	// writes. No usage flags, so they are never saved into a scene.
	ClassDB::bind_method(D_METHOD("get_replicated_position"), &Pawn::get_replicated_position);
	ClassDB::bind_method(D_METHOD("set_replicated_position", "value"), &Pawn::set_replicated_position);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "replicated_position", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT), "set_replicated_position", "get_replicated_position");
	ClassDB::bind_method(D_METHOD("get_replicated_rotation"), &Pawn::get_replicated_rotation);
	ClassDB::bind_method(D_METHOD("set_replicated_rotation", "value"), &Pawn::set_replicated_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "replicated_rotation", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT), "set_replicated_rotation", "get_replicated_rotation");
	ClassDB::bind_method(D_METHOD("get_local_role"), &Pawn::get_local_role);
	ClassDB::bind_method(D_METHOD("get_remote_role"), &Pawn::get_remote_role);
	ClassDB::bind_method(D_METHOD("get_owner_peer_id"), &Pawn::get_owner_peer_id);

	ClassDB::bind_method(D_METHOD("add_movement_input", "world_direction", "scale"), &Pawn::add_movement_input, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("consume_movement_input_vector"), &Pawn::consume_movement_input_vector);
	ClassDB::bind_method(D_METHOD("get_pending_movement_input_vector"), &Pawn::get_pending_movement_input_vector);

	ClassDB::bind_method(D_METHOD("get_player_id"), &Pawn::get_player_id);
	ClassDB::bind_method(D_METHOD("set_player_id", "value"), &Pawn::set_player_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "player_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_player_id", "get_player_id");

	ClassDB::bind_method(D_METHOD("get_camera_path"), &Pawn::get_camera_path);
	ClassDB::bind_method(D_METHOD("set_camera_path", "value"), &Pawn::set_camera_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "camera_path"), "set_camera_path", "get_camera_path");

	ClassDB::bind_method(D_METHOD("get_auto_manage_camera"), &Pawn::get_auto_manage_camera);
	ClassDB::bind_method(D_METHOD("set_auto_manage_camera", "value"), &Pawn::set_auto_manage_camera);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_manage_camera"), "set_auto_manage_camera", "get_auto_manage_camera");

	ClassDB::bind_method(D_METHOD("get_auto_possess_player"), &Pawn::get_auto_possess_player);
	ClassDB::bind_method(D_METHOD("set_auto_possess_player", "value"), &Pawn::set_auto_possess_player);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "auto_possess_player"), "set_auto_possess_player", "get_auto_possess_player");

	ClassDB::bind_method(D_METHOD("get_replicate_transform"), &Pawn::get_replicate_transform);
	ClassDB::bind_method(D_METHOD("set_replicate_transform", "value"), &Pawn::set_replicate_transform);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "replicate_transform"), "set_replicate_transform", "get_replicate_transform");

	BIND_CONSTANT(AUTO_POSSESS_DISABLED);

	ClassDB::bind_method(D_METHOD("gather_movement_input", "delta"), &Pawn::gather_movement_input);

	GDVIRTUAL_BIND(_gather_movement_input, "delta");
	GDVIRTUAL_BIND(_possessed, "controller");
	GDVIRTUAL_BIND(_unpossessed);
	GDVIRTUAL_BIND(_setup_input_component, "input_component");
	GDVIRTUAL_BIND(_get_replicated_properties);

	ADD_SIGNAL(MethodInfo("possessed", PropertyInfo(Variant::OBJECT, "controller", PROPERTY_HINT_NODE_TYPE, "Controller")));
	ADD_SIGNAL(MethodInfo("unpossessed"));
}
}
