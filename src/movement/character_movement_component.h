#ifndef CHARACTER_MOVEMENT_COMPONENT_H
#define CHARACTER_MOVEMENT_COMPONENT_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "movement/movement_types.h"
#include "movement/movement_utils.h"

using namespace godot;

namespace godot
{
class CapsuleShape3D;
class CollisionShape3D;
class Node3D;
class PhysicsBody3D;
}

namespace GFGD
{
class LayeredMove;
class MovementBackend;
class MovementMode;
class Pawn;

// Walking, falling and jumping for a pawn, with the property names Unreal's
// CharacterMovementComponent uses and none of its architecture.
//
// Attach as a child of the node being moved, beside the Pawn - the same shape
// AbilitySystemComponent has. The body itself may be any PhysicsBody3D; nothing
// here calls move_and_slide, so a CharacterBody3D is a sensible default rather
// than a requirement.
//
// Nothing about the simulation is stored on this node. The state lives in a
// MovementState object, a tick is (state, input, dt) -> state', and the body's
// transform is written exactly once, at the end. That is what would let the same
// tick be replayed against a corrected state - the thing client prediction is.
class CharacterMovementComponent : public Node
{
	GDCLASS(CharacterMovementComponent, Node)

public:
	// Plain characters rather than StringName constants on purpose. A StringName
	// built at file scope is constructed while the library is still loading,
	// before the GDExtension interface it calls into exists - which fails the DLL
	// initialisation outright, with nothing but "error 1114" to go on.
	static constexpr const char* MODE_WALKING = "Walking";
	static constexpr const char* MODE_FALLING = "Falling";
	static constexpr const char* MODE_FLYING = "Flying";
	static constexpr const char* MODE_SWIMMING = "Swimming";
	static constexpr const char* MODE_NULL = "Null";

private:
	NodePath updated_body_path;
	Dictionary modes;
	StringName starting_mode;

	// Transition rules checked in every mode, after that mode's own.
	Array transitions;

	// --- Resolved on _ready -------------------------------------------------

	PhysicsBody3D* updated_body;
	Pawn* pawn;
	MovementUtils::Body body;

	// Read off the body's collision shape. Used to tell a floor from a wall the
	// capsule happens to be grazing, and to bring the fallback ray's distance
	// back to the same measure the shape sweep reports.
	float capsule_radius;

	// The live half height: the crouched one while crouching. standing_half_height
	// is what it returns to.
	float capsule_half_height;
	float standing_half_height;

	// Resolved only when crouching is enabled, because resizing needs a shape this
	// component owns - see resolve_capsule_dimensions.
	CollisionShape3D* capsule_shape_node;
	Ref<CapsuleShape3D> capsule_shape;

	// --- Simulation ---------------------------------------------------------

	MovementBackend* backend;

	Ref<MovementState> state;
	Ref<MovementInput> input;
	Ref<ProposedMove> proposed_move;
	Ref<MovementTickParams> tick_params;
	Ref<FloorResult> current_floor;

	// Where the base was when the last tick ended. A snapshot of the world rather
	// than part of the character's state, which is why it lives here and not in
	// MovementState: no resimulation could honour it anyway, because the platform
	// has moved on since.
	Transform3D base_last_transform;
	bool has_base_last_transform;

	// Scratch for the narrow floor test, so the perch path does not allocate and
	// does not have to write its answer over the result it is reading from.
	Ref<FloorResult> perch_floor;

	StringName queued_mode;
	bool has_queued_mode;

	// Scratch each layered move writes its proposal into, so mixing does not
	// allocate per move per tick.
	Ref<ProposedMove> layered_proposal;

	// Latched by the input pump and cleared once a tick has consumed it, so a
	// jump pressed and released between two physics ticks is not swallowed.
	bool jump_pressed_latch;

	// Same shape as the jump latch: crouch() from a render frame, consumed on the
	// physics tick.
	bool crouch_latch;

	// --- Settings -----------------------------------------------------------

	Vector3 up_direction;
	float gravity;
	float gravity_scale;

	float max_walk_speed;

	// The floor a half-pushed stick is allowed to fall to. Zero means a stick
	// pushed a tenth of the way walks at a tenth of the speed.
	float min_analog_walk_speed;

	// Crouching. Unreal defaults its equivalent off; this defaults on, because a
	// want_crouch that silently does nothing is a worse first experience than a
	// character that crouches when asked.
	bool can_crouch;
	float crouched_half_height;
	float max_walk_speed_crouched;
	float max_acceleration;
	float ground_friction;
	float braking_deceleration_walking;
	float braking_friction_factor;
	bool use_separate_braking_friction;
	float braking_friction;

	float walkable_floor_angle;
	float walkable_floor_cos;
	float floor_sweep_distance;
	float max_step_height;

	// Zero disables perching, which is also Unreal's default. Raise it and a
	// character keeps its footing with part of the capsule hanging over an edge.
	float perch_radius_threshold;

	// How far above a walkable floor a perched character may hang, on top of
	// max_step_height.
	float perch_additional_height;

	// False stops the character at a ledge instead of letting it step off.
	bool can_walk_off_ledges;

	float max_fly_speed;
	float braking_deceleration_flying;

	float max_swim_speed;
	float braking_deceleration_swimming;

	// The push a jump gives while swimming - what gets a character out of a pool
	// rather than bobbing against the lip of it.
	float out_of_water_jump_velocity;

	float jump_velocity;
	float air_control;
	float falling_lateral_friction;
	float braking_deceleration_falling;

	float max_simulation_time_step;
	int max_simulation_iterations;
	int max_slides;

	bool orient_rotation_to_movement;
	float rotation_rate_deg;

	// Carries the character along with whatever it is standing on.
	bool move_with_base;

	// Turns with it too, about the up axis only - a tilting platform moves a
	// character without tipping it over.
	bool rotate_with_base;

	// Hands the platform's own velocity to the character when it leaves, so a
	// jump from a moving lift keeps the lift's momentum.
	bool impart_base_velocity;

	// Sends the active mode name from the server to everyone else. Turn it off for
	// a pawn whose mode nothing outside the simulation has to know.
	bool replicate_movement_mode;

public:
	CharacterMovementComponent();
	~CharacterMovementComponent();

	virtual void _ready() override;
	virtual void _physics_process(double delta) override;

	// One simulation step. Called by the backend, which is what decides how many
	// of them a frame gets - one when the server is simulating, several when a
	// client is replaying inputs after a correction.
	void simulate(double delta);

	// --- Modes --------------------------------------------------------------

	// Takes effect at the next mode resolution inside the tick, never in the
	// middle of one. A mode swapped underneath a half-finished move would leave
	// the state describing a position the body is not in.
	void queue_next_mode(const StringName& mode_name);

	StringName get_movement_mode() const;
	void set_movement_mode(const StringName& mode_name);

	Ref<MovementMode> find_mode(const StringName& mode_name) const;

	// --- State --------------------------------------------------------------

	// --- Layered moves --------------------------------------------------------

	// Lays motion over whatever mode is running - a dash, a knockback, a gust.
	// Starts on the next simulated tick.
	void queue_layered_move(const Ref<LayeredMove>& move);

	// Drops every active and queued move at once, applying each one's finish
	// velocity as it goes.
	void cancel_all_layered_moves();

	Array get_active_layered_moves() const { return state->get_active_layered_moves(); }

	// The backend's simulation clock, which is what a layered move measures its
	// duration against.
	double get_sim_time_ms() const;

	Ref<MovementState> get_state() const { return state; }
	Ref<MovementInput> get_input() const { return input; }
	Ref<FloorResult> get_current_floor() const { return current_floor; }
	Ref<FloorResult> get_perch_floor() const { return perch_floor; }

	Vector3 get_velocity() const;
	void set_velocity(const Vector3& value);

	bool is_on_ground() const;
	bool is_falling() const;

	PhysicsBody3D* get_updated_body() const { return updated_body; }
	Pawn* get_pawn() const { return pawn; }

	// C++ only: the handle the solver works through.
	const MovementUtils::Body& get_body() const { return body; }

	// --- The solver, for modes written in GDScript ---------------------------
	//
	// A mode is only a real extension point if it can ask the same questions the
	// built-in ones ask. These forward to the stateless solver with this
	// component's body, and they take the transform to work from rather than
	// reading the node - the same rule the C++ side follows, and for the same
	// reason: a tick has to be replayable.

	// { transform, collided, remaining, normal, point, collider }
	Dictionary sweep_from(const Transform3D& from, const Vector3& motion) const;

	// { transform, velocity, hit_wall, hit_walkable, stepped_up }
	Dictionary slide_move_from(const Transform3D& from, const Vector3& motion, const Vector3& velocity) const;

	// Ground move: slides, and climbs anything short enough to step over.
	Dictionary move_along_floor_from(const Transform3D& from, const Vector3& motion, const Vector3& velocity) const;

	// A fresh result each call, so a script can hold on to one.
	Ref<FloorResult> find_floor_at(const Transform3D& from) const;

	// Snaps a transform into the band the solver keeps a standing character in.
	Transform3D adjust_floor_height_at(const Transform3D& from, const Ref<FloorResult>& floor) const;

	// The acceleration, friction and braking model the built-in modes use.
	Vector3 compute_velocity(const Vector3& velocity, const Vector3& acceleration, float friction, float braking_deceleration, float max_speed, double delta) const;

	// The ground solver's knobs, gathered from the settings above. C++ only, for
	// the same reason get_body is.
	MovementUtils::GroundMoveSettings get_ground_move_settings() const;

	float get_capsule_radius() const { return capsule_radius; }
	float get_capsule_half_height() const { return capsule_half_height; }
	float get_walkable_floor_cos() const { return walkable_floor_cos; }

	// The braking friction actually in force, which is the separate one only when
	// the game asked for it. Two numbers exist because steering and stopping are
	// not the same feel, and a game that wants them equal should not have to keep
	// them in step by hand.
	float get_effective_braking_friction() const { return use_separate_braking_friction ? braking_friction : ground_friction; }

	// --- Settings -----------------------------------------------------------

	NodePath get_updated_body_path() const { return updated_body_path; }
	void set_updated_body_path(const NodePath& value) { updated_body_path = value; }

	Dictionary get_modes() const { return modes; }
	void set_modes(const Dictionary& value) { modes = value; }

	StringName get_starting_mode() const { return starting_mode; }
	void set_starting_mode(const StringName& value) { starting_mode = value; }

	Vector3 get_up_direction() const { return up_direction; }
	void set_up_direction(const Vector3& value);

	float get_gravity() const { return gravity; }
	void set_gravity(float value) { gravity = value; }

	float get_gravity_scale() const { return gravity_scale; }
	void set_gravity_scale(float value) { gravity_scale = value; }

	float get_max_walk_speed() const { return max_walk_speed; }
	void set_max_walk_speed(float value) { max_walk_speed = value; }

	float get_min_analog_walk_speed() const { return min_analog_walk_speed; }
	void set_min_analog_walk_speed(float value) { min_analog_walk_speed = value; }

	bool get_can_crouch() const { return can_crouch; }
	void set_can_crouch(bool value) { can_crouch = value; }

	float get_crouched_half_height() const { return crouched_half_height; }
	void set_crouched_half_height(float value) { crouched_half_height = value; }

	float get_max_walk_speed_crouched() const { return max_walk_speed_crouched; }
	void set_max_walk_speed_crouched(float value) { max_walk_speed_crouched = value; }

	bool is_crouching() const { return state->get_is_crouching(); }

	// The walking cap in force right now, which is the crouched one while
	// crouching.
	float get_effective_max_walk_speed() const { return is_crouching() ? max_walk_speed_crouched : max_walk_speed; }

	// Asks to crouch or stand on the next tick. Standing is a request rather than
	// an order: a character under a low ceiling stays down until there is room.
	void crouch();
	void un_crouch();

	// The speed cap for an input of this length: a stick pushed half way walks at
	// half speed, not at full speed reached more slowly.
	//
	// Scaling only the acceleration - which is what falls out of using the input
	// vector as an acceleration and nothing else - lets every input eventually
	// reach the same top speed, so a gentle push is a slow start rather than a
	// walk. Unreal calls the same factor AnalogInputModifier.
	float get_analog_max_speed(const Vector3& move_input, float base_max_speed) const;

	float get_max_acceleration() const { return max_acceleration; }
	void set_max_acceleration(float value) { max_acceleration = value; }

	float get_ground_friction() const { return ground_friction; }
	void set_ground_friction(float value) { ground_friction = value; }

	float get_braking_deceleration_walking() const { return braking_deceleration_walking; }
	void set_braking_deceleration_walking(float value) { braking_deceleration_walking = value; }

	float get_braking_friction_factor() const { return braking_friction_factor; }
	void set_braking_friction_factor(float value) { braking_friction_factor = value; }

	bool get_use_separate_braking_friction() const { return use_separate_braking_friction; }
	void set_use_separate_braking_friction(bool value) { use_separate_braking_friction = value; }

	float get_braking_friction() const { return braking_friction; }
	void set_braking_friction(float value) { braking_friction = value; }

	float get_walkable_floor_angle() const { return walkable_floor_angle; }
	void set_walkable_floor_angle(float value);

	float get_floor_sweep_distance() const { return floor_sweep_distance; }
	void set_floor_sweep_distance(float value) { floor_sweep_distance = value; }

	float get_max_step_height() const { return max_step_height; }
	void set_max_step_height(float value) { max_step_height = value; }

	float get_perch_radius_threshold() const { return perch_radius_threshold; }
	void set_perch_radius_threshold(float value) { perch_radius_threshold = value; }

	float get_perch_additional_height() const { return perch_additional_height; }
	void set_perch_additional_height(float value) { perch_additional_height = value; }

	bool get_can_walk_off_ledges() const { return can_walk_off_ledges; }
	void set_can_walk_off_ledges(bool value) { can_walk_off_ledges = value; }

	float get_max_swim_speed() const { return max_swim_speed; }
	void set_max_swim_speed(float value) { max_swim_speed = value; }

	float get_braking_deceleration_swimming() const { return braking_deceleration_swimming; }
	void set_braking_deceleration_swimming(float value) { braking_deceleration_swimming = value; }

	float get_out_of_water_jump_velocity() const { return out_of_water_jump_velocity; }
	void set_out_of_water_jump_velocity(float value) { out_of_water_jump_velocity = value; }

	// The WaterVolume at a point, or null. Asked of the physics server rather
	// than tracked through Area3D signals, so it answers for the transform being
	// simulated rather than for wherever the body last happened to be.
	Object* find_water_volume_at(const Vector3& point) const;

	// How much of the character the water is holding up: 0 out of it, 0.5 up to
	// the chest, 1 under the surface. Wading is not swimming, which is why the
	// middle of the capsule is what decides and not its feet.
	float get_immersion_depth_at(const Transform3D& from) const;

	float get_max_fly_speed() const { return max_fly_speed; }
	void set_max_fly_speed(float value) { max_fly_speed = value; }

	float get_braking_deceleration_flying() const { return braking_deceleration_flying; }
	void set_braking_deceleration_flying(float value) { braking_deceleration_flying = value; }

	Array get_transitions() const { return transitions; }
	void set_transitions(const Array& value) { transitions = value; }

	float get_jump_velocity() const { return jump_velocity; }
	void set_jump_velocity(float value) { jump_velocity = value; }

	float get_air_control() const { return air_control; }
	void set_air_control(float value) { air_control = value; }

	float get_falling_lateral_friction() const { return falling_lateral_friction; }
	void set_falling_lateral_friction(float value) { falling_lateral_friction = value; }

	float get_braking_deceleration_falling() const { return braking_deceleration_falling; }
	void set_braking_deceleration_falling(float value) { braking_deceleration_falling = value; }

	float get_max_simulation_time_step() const { return max_simulation_time_step; }
	void set_max_simulation_time_step(float value) { max_simulation_time_step = value; }

	int get_max_simulation_iterations() const { return max_simulation_iterations; }
	void set_max_simulation_iterations(int value) { max_simulation_iterations = value; }

	int get_max_slides() const { return max_slides; }
	void set_max_slides(int value) { max_slides = value; }

	bool get_orient_rotation_to_movement() const { return orient_rotation_to_movement; }
	void set_orient_rotation_to_movement(bool value) { orient_rotation_to_movement = value; }

	float get_rotation_rate_deg() const { return rotation_rate_deg; }
	void set_rotation_rate_deg(float value) { rotation_rate_deg = value; }

	bool get_move_with_base() const { return move_with_base; }
	void set_move_with_base(bool value) { move_with_base = value; }

	bool get_rotate_with_base() const { return rotate_with_base; }
	void set_rotate_with_base(bool value) { rotate_with_base = value; }

	bool get_impart_base_velocity() const { return impart_base_velocity; }
	void set_impart_base_velocity(bool value) { impart_base_velocity = value; }

	// Records what the character ended the tick standing on, and where it stands
	// relative to it. Called once per simulated frame, after the modes have run.
	void save_base_location();

	bool get_replicate_movement_mode() const { return replicate_movement_mode; }
	void set_replicate_movement_mode(bool value) { replicate_movement_mode = value; }

	// Asks for a jump on the next tick. Latched rather than acted on, because
	// input is polled on the render frame and the simulation runs on the physics
	// one; the two do not line up.
	void jump();
	void stop_jumping();

protected:
	static void _bind_methods();

private:
	void resolve_updated_body();
	void resolve_capsule_dimensions();

	// Starts or ends a crouch, resizing the capsule and shifting the body so the
	// feet stay where they were rather than the centre.
	void update_crouch_state();
	void apply_capsule_half_height(float value);
	void register_default_modes();

	// Mirrors the active mode name from the server. Called from _ready on every
	// peer, because a MultiplayerSynchronizer only pairs up with its counterpart
	// when both sides sit at the same path.
	//
	// Unreal replicates the same thing as ACharacter::ReplicatedMovementMode, but
	// under COND_SimulatedOnly: an autonomous proxy there predicts its own mode,
	// so sending it one would fight the prediction. Nothing here predicts yet, so
	// the owning client is a receiver like everybody else and gets it too. That
	// condition is what will have to be added along with prediction, not before.
	void setup_replication();

	// Moves the character by however much its base moved, by rebuilding the world
	// transform from the base's current one and the stored relative offset.
	void update_based_movement();

	// The node behind MovementState::base_id, or null once it is gone.
	Node3D* resolve_base() const;

	// Reads what the player asked for and clamps it. The clamp matters: the pawn
	// accumulates input on the render frame and this consumes it on the physics
	// frame, so at 144Hz render and 60Hz physics the vector has been added twice
	// and is twice as long. Left alone, walking speed would rise with the frame
	// rate - and the symptom, a character that is faster on a better machine, is
	// a miserable thing to track down after the fact.
	void gather_input(int64_t frame, double tick_delta);

	void run_mode_tick(double delta);

	// The active mode's own transition rules first, then the global ones; the
	// first that names a mode wins. Skipped entirely when the mode itself already
	// asked for a change - a mode that acted has the last word on its own exit.
	void evaluate_transitions(const Ref<MovementMode>& mode);

	// Starts anything queued, then blends every active move into the proposal the
	// mode just made. Runs between generate_move and simulation_tick, which is the
	// only window in which a proposal exists and nothing has been swept yet.
	void mix_layered_moves(const Ref<MovementTickParams>& params);

	// Drops moves whose time is up, applying their finish velocity to the state.
	void retire_finished_layered_moves();
	void resolve_queued_mode();
	void apply_state_to_body();
	void apply_rotation(double delta);
};
}

#endif
