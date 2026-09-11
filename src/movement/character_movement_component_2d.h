#ifndef CHARACTER_MOVEMENT_COMPONENT_2D_H
#define CHARACTER_MOVEMENT_COMPONENT_2D_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "movement/movement_types.h"
#include "movement/movement_utils_2d.h"

using namespace godot;

namespace godot
{
class CapsuleShape2D;
class CollisionShape2D;
class Node2D;
class PhysicsBody2D;
}

namespace GFGD
{
class LayeredMove;
class MovementBackend;
class MovementMode;
class Pawn;

// Walking, falling and jumping for a 2D pawn.
//
// The 3D component's twin, and deliberately a twin rather than a shared base:
// Godot keeps 2D and 3D in separate hierarchies all the way down, and GFGD
// already follows that with PlayerStart2D and PlayerStart3D.
//
// What is shared is everything that has no dimension: MovementState,
// MovementInput, ProposedMove, FloorResult and MovementMode are the same classes
// as in 3D. A 2D state stores its position in the x and y of a Vector3 and
// leaves z alone, which keeps one set of types, one mode contract and one
// prediction story instead of two.
//
// Units are pixels. The defaults are Unreal's centimetre values unchanged, which
// is what the usual "a metre is a hundred pixels" convention makes them; the 3D
// component divides the same numbers by a hundred.
class CharacterMovementComponent2D : public Node
{
	GDCLASS(CharacterMovementComponent2D, Node)

public:
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

	PhysicsBody2D* updated_body;
	Pawn* pawn;
	MovementUtils2D::Body body;

	float capsule_radius;

	// The live half height: the crouched one while crouching.
	float capsule_half_height;
	float standing_half_height;

	// Resolved only when crouching is enabled, because resizing needs a shape this
	// component owns.
	CollisionShape2D* capsule_shape_node;
	Ref<CapsuleShape2D> capsule_shape;

	MovementBackend* backend;

	Ref<MovementState> state;
	Ref<MovementInput> input;
	Ref<ProposedMove> proposed_move;
	Ref<MovementTickParams> tick_params;
	Ref<FloorResult> current_floor;

	// Where the base was at the end of the last tick, so this tick can move the
	// character by the difference.
	Transform2D base_last_transform;
	bool has_base_last_transform;

	StringName queued_mode;
	bool has_queued_mode;
	bool jump_pressed_latch;
	bool crouch_latch;

	// Scratch each layered move writes its proposal into, so mixing does not
	// allocate per move per tick.
	Ref<ProposedMove> layered_proposal;

	// Up is negative Y here. Godot's 2D screen axis points down, so every sign in
	// the solver depends on this being right and nothing else catches it.
	Vector2 up_direction;

	float gravity;
	float gravity_scale;

	float max_walk_speed;
	float min_analog_walk_speed;

	bool can_crouch;
	float crouched_half_height;
	float max_walk_speed_crouched;
	float max_acceleration;
	float ground_friction;
	float braking_deceleration_walking;
	float braking_friction_factor;

	float walkable_floor_angle;
	float walkable_floor_cos;
	float floor_sweep_distance;
	float max_step_height;

	float max_fly_speed;
	float braking_deceleration_flying;

	float max_swim_speed;
	float braking_deceleration_swimming;

	// A push off the surface, not a jump off the floor: it is what gets a
	// character out of a pool rather than bobbing against the lip of it.
	float out_of_water_jump_velocity;

	float jump_velocity;
	float air_control;
	float falling_lateral_friction;
	float braking_deceleration_falling;

	float max_simulation_time_step;
	int max_simulation_iterations;
	int max_slides;

	// Carries the character along with whatever it is standing on. There is no
	// rotate_with_base twin of this: the 2D component writes only a position back
	// to the body, so there is no character rotation for a platform to turn.
	bool move_with_base;

	// Hands the platform's own velocity to the character when it leaves, so a
	// jump from a moving lift keeps the lift's momentum.
	bool impart_base_velocity;

public:
	CharacterMovementComponent2D();
	~CharacterMovementComponent2D();

	virtual void _ready() override;
	virtual void _physics_process(double delta) override;

	void simulate(double delta);

	void queue_next_mode(const StringName& mode_name);
	StringName get_movement_mode() const;
	void set_movement_mode(const StringName& mode_name);
	Ref<MovementMode> find_mode(const StringName& mode_name) const;

	// --- Layered moves --------------------------------------------------------

	void queue_layered_move(const Ref<LayeredMove>& move);
	void cancel_all_layered_moves();
	Array get_active_layered_moves() const { return state->get_active_layered_moves(); }
	double get_sim_time_ms() const;

	Array get_transitions() const { return transitions; }
	void set_transitions(const Array& value) { transitions = value; }

	Ref<MovementState> get_state() const { return state; }
	Ref<MovementInput> get_input() const { return input; }
	Ref<FloorResult> get_current_floor() const { return current_floor; }

	Vector2 get_velocity() const;
	void set_velocity(const Vector2& value);

	bool is_on_ground() const;
	bool is_falling() const;

	PhysicsBody2D* get_updated_body() const { return updated_body; }
	Pawn* get_pawn() const { return pawn; }

	const MovementUtils2D::Body& get_body() const { return body; }
	MovementUtils2D::GroundMoveSettings get_ground_move_settings() const;

	float get_capsule_radius() const { return capsule_radius; }
	float get_capsule_half_height() const { return capsule_half_height; }
	float get_walkable_floor_cos() const { return walkable_floor_cos; }

	// --- The solver, for modes written in GDScript ---------------------------

	Dictionary sweep_from(const Transform2D& from, const Vector2& motion) const;
	Dictionary slide_move_from(const Transform2D& from, const Vector2& motion, const Vector2& velocity) const;
	Dictionary move_along_floor_from(const Transform2D& from, const Vector2& motion, const Vector2& velocity) const;
	Ref<FloorResult> find_floor_at(const Transform2D& from) const;
	Transform2D adjust_floor_height_at(const Transform2D& from, const Ref<FloorResult>& floor) const;
	Vector2 compute_velocity(const Vector2& velocity, const Vector2& acceleration, float friction, float braking_deceleration, float max_speed, double delta) const;

	float get_analog_max_speed(const Vector2& move_input, float base_max_speed) const;

	// --- Water ---------------------------------------------------------------

	// The WaterVolume2D covering a point, or null. Asked from the transform being
	// simulated, never from the body's own, so a replayed tick sees what was true
	// at that tick.
	Object* find_water_volume_at(const Vector2& point) const;

	// How much of the capsule the water is holding up, from 0 (dry) to 1 (head
	// under). Everything between is sampled up the capsule, because the exact
	// figure is what damps the rise at the surface and what scales the buoyancy.
	float get_immersion_depth_at(const Transform2D& from) const;

	// --- Settings -----------------------------------------------------------

	NodePath get_updated_body_path() const { return updated_body_path; }
	void set_updated_body_path(const NodePath& value) { updated_body_path = value; }

	Dictionary get_modes() const { return modes; }
	void set_modes(const Dictionary& value) { modes = value; }

	StringName get_starting_mode() const { return starting_mode; }
	void set_starting_mode(const StringName& value) { starting_mode = value; }

	Vector2 get_up_direction() const { return up_direction; }
	void set_up_direction(const Vector2& value);

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
	float get_effective_max_walk_speed() const { return is_crouching() ? max_walk_speed_crouched : max_walk_speed; }

	void crouch();
	void un_crouch();

	float get_max_acceleration() const { return max_acceleration; }
	void set_max_acceleration(float value) { max_acceleration = value; }

	float get_ground_friction() const { return ground_friction; }
	void set_ground_friction(float value) { ground_friction = value; }

	float get_braking_deceleration_walking() const { return braking_deceleration_walking; }
	void set_braking_deceleration_walking(float value) { braking_deceleration_walking = value; }

	float get_braking_friction_factor() const { return braking_friction_factor; }
	void set_braking_friction_factor(float value) { braking_friction_factor = value; }

	float get_walkable_floor_angle() const { return walkable_floor_angle; }
	void set_walkable_floor_angle(float value);

	float get_floor_sweep_distance() const { return floor_sweep_distance; }
	void set_floor_sweep_distance(float value) { floor_sweep_distance = value; }

	float get_max_step_height() const { return max_step_height; }
	void set_max_step_height(float value) { max_step_height = value; }

	float get_max_fly_speed() const { return max_fly_speed; }
	void set_max_fly_speed(float value) { max_fly_speed = value; }

	float get_braking_deceleration_flying() const { return braking_deceleration_flying; }
	void set_braking_deceleration_flying(float value) { braking_deceleration_flying = value; }

	float get_max_swim_speed() const { return max_swim_speed; }
	void set_max_swim_speed(float value) { max_swim_speed = value; }

	float get_braking_deceleration_swimming() const { return braking_deceleration_swimming; }
	void set_braking_deceleration_swimming(float value) { braking_deceleration_swimming = value; }

	float get_out_of_water_jump_velocity() const { return out_of_water_jump_velocity; }
	void set_out_of_water_jump_velocity(float value) { out_of_water_jump_velocity = value; }

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

	bool get_move_with_base() const { return move_with_base; }
	void set_move_with_base(bool value) { move_with_base = value; }

	bool get_impart_base_velocity() const { return impart_base_velocity; }
	void set_impart_base_velocity(bool value) { impart_base_velocity = value; }

	// Records what the character ended the tick standing on, and where it stands
	// relative to it. Called once per simulated frame, after the modes have run.
	void save_base_location();

	void jump();
	void stop_jumping();

protected:
	static void _bind_methods();

private:
	void resolve_updated_body();
	void resolve_capsule_dimensions();
	// Moves the character by however much its base moved.
	void update_based_movement();

	// The node behind MovementState::base_id, or null once it is gone.
	Node2D* resolve_base() const;

	void update_crouch_state();
	void apply_capsule_half_height(float value);
	void register_default_modes();
	void gather_input(int64_t frame, double tick_delta);
	void run_mode_tick(double delta);
	void evaluate_transitions(const Ref<MovementMode>& mode);
	void mix_layered_moves(const Ref<MovementTickParams>& params);
	void retire_finished_layered_moves();
	void resolve_queued_mode();
	void apply_state_to_body();
};
}

#endif
