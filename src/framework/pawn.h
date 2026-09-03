#ifndef PAWN_H
#define PAWN_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "framework/controller.h"
#include "framework/input_component.h"

using namespace godot;

namespace GFGD
{
class PlayerState;
class World;

// Attach as a child of the node a player drives to make it possessable.
//
// It is a child rather than the root because the root is whatever the game needs
// it to be - a CharacterBody3D, an Area2D, a RigidBody3D - and that choice
// belongs to the game. Everything here works on get_pawn_root(), so a script on
// this node speaks for the whole thing.
class Pawn : public Node
{
	GDCLASS(Pawn, Node)

public:
	// -1 means nothing happens on its own, which is what a pawn spawned by the
	// game mode wants. 0 and up is the index of the local player that takes it
	// over as soon as it is in the tree.
	static constexpr int AUTO_POSSESS_DISABLED = -1;

	// The Pawn belonging to a pawn's root node: the root itself if it is one,
	// otherwise the first one below it.
	static Pawn* find_in(Node* pawn_root);

private:
	Controller* controller;
	NodePath camera_path;

	// A pawn that owns its camera wants possession to make it current. A pawn
	// filmed by a fixed arena camera does not, and the recursive search for a
	// camera it does not have is both wasted work and a silent way to steal the
	// view from the camera the level set up.
	bool auto_manage_camera;

	int auto_possess_player;

	// Mirrors the root's position and rotation from the server. Turn it off for a
	// pawn whose movement is driven some other way.
	bool replicate_transform;

	// Set when the framework spawns it, and part of the pawn's node name, so the
	// same pawn is addressed the same way everywhere.
	int player_id;

	Vector3 movement_input;

public:
	Pawn();
	~Pawn();

	virtual void _ready() override;

	void possessed_by(Controller* new_controller);
	void unpossessed();
	void setup_input_component(InputComponent* input_component);

	Controller* get_controller() const { return controller; }
	Node* get_pawn_root() const { return get_parent(); }
	PlayerState* get_player_state() const;

	// True where the human driving this pawn is sitting - the only place that may
	// read input for it.
	bool is_locally_controlled() const;
	bool is_player_controlled() const;
	bool is_bot_controlled() const;

	bool has_authority() const;
	int get_local_role() const;
	int get_remote_role() const;
	int get_owner_peer_id() const;

	// Accumulates a direction to move in this frame. The pawn's own code takes it
	// out with consume_movement_input_vector and applies it however it likes,
	// which keeps "what the player asked for" separate from "how it moves".
	void add_movement_input(const Vector3& world_direction, float scale = 1.0f);
	Vector3 consume_movement_input_vector();
	Vector3 get_pending_movement_input_vector() const { return movement_input; }

	int get_player_id() const { return player_id; }
	void set_player_id(int value) { player_id = value; }

	NodePath get_camera_path() const { return camera_path; }
	void set_camera_path(const NodePath& value) { camera_path = value; }

	bool get_auto_manage_camera() const { return auto_manage_camera; }
	void set_auto_manage_camera(bool value) { auto_manage_camera = value; }

	int get_auto_possess_player() const { return auto_possess_player; }
	void set_auto_possess_player(int value) { auto_possess_player = value; }

	bool get_replicate_transform() const { return replicate_transform; }
	void set_replicate_transform(bool value) { replicate_transform = value; }

	World* get_world() const;

	GDVIRTUAL1(_possessed, Controller*)
	GDVIRTUAL0(_unpossessed)
	GDVIRTUAL1(_setup_input_component, InputComponent*)

	// Names of this node's own properties to keep in step with the server's copy.
	GDVIRTUAL0R(PackedStringArray, _get_replicated_properties)

protected:
	static void _bind_methods();

private:
	void setup_replication();
	void apply_auto_possess();
};
}

#endif
