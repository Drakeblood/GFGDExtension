#ifndef MOVEMENT_TYPES_H
#define MOVEMENT_TYPES_H

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

namespace GFGD
{
class CharacterMovementComponent;
class CharacterMovementComponent2D;

// What the player asked for on one simulation tick.
//
// It is an object rather than a set of arguments because it is the half of the
// simulation that has to travel: a predicted client sends this and nothing else,
// and the server replays it. Everything needed to reproduce a tick is here.
class MovementInput : public RefCounted
{
	GDCLASS(MovementInput, RefCounted)

private:
	// World space, length clamped to 1. The clamp is not cosmetic - see
	// CharacterMovementComponent::gather_input.
	Vector3 move_input;

	bool want_jump;

	// Read by no mode yet; crouching arrives with the stance work. It is here
	// because a mode written in GDScript should not have to wait for it.
	bool want_crouch;

	// The tick this input belongs to. Unused while the server is the only thing
	// simulating, and the one field a client would have to send for a server to
	// be able to say "I processed up to here" - which is the whole basis of
	// reconciliation. Eight bytes now is cheaper than a format change later.
	int64_t frame;

public:
	MovementInput();
	~MovementInput();

	Vector3 get_move_input() const { return move_input; }
	void set_move_input(const Vector3& value) { move_input = value; }

	bool get_want_jump() const { return want_jump; }
	void set_want_jump(bool value) { want_jump = value; }

	bool get_want_crouch() const { return want_crouch; }
	void set_want_crouch(bool value) { want_crouch = value; }

	int64_t get_frame() const { return frame; }
	void set_frame(int64_t value) { frame = value; }

	void reset();

protected:
	static void _bind_methods();
};

// Everything the simulation carries from one tick to the next.
//
// Nothing here lives on the component. That is the whole point: a tick is
// (state, input, dt) -> state', so the same code can be run twice on one frame
// with different starting states, which is what a resimulation is. A field kept
// on the component instead would be invisible to that and would survive a
// rollback it should not have survived.
class MovementState : public RefCounted
{
	GDCLASS(MovementState, RefCounted)

private:
	Vector3 position;
	Basis rotation;
	Vector3 velocity;
	StringName movement_mode;
	bool is_crouching;

	// What the character is standing on, and where it is standing relative to it.
	//
	// The relative transform is stored rather than the base's previous position,
	// which is the difference that matters: rebuilding the world position from
	// the base's *current* transform needs nothing remembered about the past, so
	// a tick can be replayed without a record of where the platform used to be -
	// a record no resimulation could honour anyway, because the platform has
	// moved on.
	int64_t base_id;
	Vector3 base_relative_position;
	Basis base_relative_rotation;

	// Motion laid over the active mode. Here rather than on the component because
	// this is simulation state: a replayed tick has to start with the same moves
	// running, and a rollback has to take back the ones that were queued after the
	// point being rolled back to.
	//
	// Copied shallowly - the entries are shared, only the lists are new. Correct
	// while time only moves forward, which is all that happens today; replaying a
	// tick would need the moves themselves duplicated, because their start time is
	// per-instance.
	Array queued_layered_moves;
	Array active_layered_moves;

	// A mode's own state, for anything the framework does not know about. A
	// Dictionary because Godot can already put one on the wire; a mode written
	// in GDScript gets persistent state without a C++ change.
	Dictionary extra;

public:
	MovementState();
	~MovementState();

	Vector3 get_position() const { return position; }
	void set_position(const Vector3& value) { position = value; }

	Basis get_rotation() const { return rotation; }
	void set_rotation(const Basis& value) { rotation = value; }

	Vector3 get_velocity() const { return velocity; }
	void set_velocity(const Vector3& value) { velocity = value; }

	StringName get_movement_mode() const { return movement_mode; }
	void set_movement_mode(const StringName& value) { movement_mode = value; }

	bool get_is_crouching() const { return is_crouching; }
	void set_is_crouching(bool value) { is_crouching = value; }

	Dictionary get_extra() const { return extra; }
	void set_extra(const Dictionary& value) { extra = value; }

	int64_t get_base_id() const { return base_id; }
	void set_base_id(int64_t value) { base_id = value; }

	Vector3 get_base_relative_position() const { return base_relative_position; }
	void set_base_relative_position(const Vector3& value) { base_relative_position = value; }

	Basis get_base_relative_rotation() const { return base_relative_rotation; }
	void set_base_relative_rotation(const Basis& value) { base_relative_rotation = value; }

	bool has_base() const { return base_id != 0; }
	void clear_base();

	Array get_queued_layered_moves() const { return queued_layered_moves; }
	void set_queued_layered_moves(const Array& value) { queued_layered_moves = value; }

	Array get_active_layered_moves() const { return active_layered_moves; }
	void set_active_layered_moves(const Array& value) { active_layered_moves = value; }

	void copy_from(const Ref<MovementState>& other);
	Ref<MovementState> duplicate_state() const;

	// Whether a client holding this state would have to snap to authority_state
	// and replay. Nothing calls it yet; it is the decision a predicted client
	// makes, and it belongs on the state rather than in the netcode because only
	// the state knows which of its fields matter.
	bool should_reconcile(const Ref<MovementState>& authority_state, float position_tolerance, float velocity_tolerance) const;

protected:
	static void _bind_methods();
};

// What a movement mode asks for, before anything has been swept or resolved.
//
// The split between proposing a move and executing it is the reason this type
// exists. A proposal is side effect free, so proposals can be blended - which is
// what makes a dash on top of a walk possible without the walking code knowing
// about dashes. Fusing the two, which is what a single "do the movement" method
// would be, forecloses that permanently.
class ProposedMove : public RefCounted
{
	GDCLASS(ProposedMove, RefCounted)

public:
	// How this proposal combines with others. Only OVERRIDE_VELOCITY is acted on
	// while a mode is the only thing proposing; the rest describe the blend that
	// layered moves will need.
	enum MixMode
	{
		ADDITIVE_VELOCITY = 0,
		OVERRIDE_VELOCITY = 1,
		OVERRIDE_ALL = 2,
		OVERRIDE_ALL_EXCEPT_VERTICAL = 3,
	};

private:
	StringName preferred_mode;
	Vector3 direction_intent;
	Vector3 linear_velocity;
	Vector3 angular_velocity_deg;
	bool has_dir_intent;
	MixMode mix_mode;

public:
	ProposedMove();
	~ProposedMove();

	StringName get_preferred_mode() const { return preferred_mode; }
	void set_preferred_mode(const StringName& value) { preferred_mode = value; }

	Vector3 get_direction_intent() const { return direction_intent; }
	void set_direction_intent(const Vector3& value) { direction_intent = value; }

	Vector3 get_linear_velocity() const { return linear_velocity; }
	void set_linear_velocity(const Vector3& value) { linear_velocity = value; }

	Vector3 get_angular_velocity_deg() const { return angular_velocity_deg; }
	void set_angular_velocity_deg(const Vector3& value) { angular_velocity_deg = value; }

	bool get_has_dir_intent() const { return has_dir_intent; }
	void set_has_dir_intent(bool value) { has_dir_intent = value; }

	MixMode get_mix_mode() const { return mix_mode; }
	void set_mix_mode(MixMode value) { mix_mode = value; }

	void reset();

protected:
	static void _bind_methods();
};

// The answer to "is there ground under this, and may it be stood on".
//
// walkable_floor is not implied by blocking_hit: a steep slope is a hit that is
// not a floor, and telling those apart is what keeps a character from standing
// on a wall.
class FloorResult : public RefCounted
{
	GDCLASS(FloorResult, RefCounted)

private:
	bool blocking_hit;
	bool walkable_floor;

	// True when the sweep found nothing usable and the downward ray is what
	// reported this floor. The distance to trust is then line_dist.
	bool line_trace;

	float floor_dist;
	float line_dist;
	Vector3 normal;
	Vector3 point;
	Object* collider;

	// The surface's own motion, as the sweep reported it. Only bodies Godot
	// tracks the movement of fill this in - an AnimatableBody3D does, a
	// StaticBody3D pushed around by a script does not.
	Vector3 collider_velocity;

public:
	FloorResult();
	~FloorResult();

	bool get_blocking_hit() const { return blocking_hit; }
	void set_blocking_hit(bool value) { blocking_hit = value; }

	bool get_walkable_floor() const { return walkable_floor; }
	void set_walkable_floor(bool value) { walkable_floor = value; }

	bool get_line_trace() const { return line_trace; }
	void set_line_trace(bool value) { line_trace = value; }

	float get_floor_dist() const { return floor_dist; }
	void set_floor_dist(float value) { floor_dist = value; }

	float get_line_dist() const { return line_dist; }
	void set_line_dist(float value) { line_dist = value; }

	Vector3 get_normal() const { return normal; }
	void set_normal(const Vector3& value) { normal = value; }

	Vector3 get_point() const { return point; }
	void set_point(const Vector3& value) { point = value; }

	Object* get_collider() const { return collider; }
	void set_collider(Object* value) { collider = value; }

	Vector3 get_collider_velocity() const { return collider_velocity; }
	void set_collider_velocity(const Vector3& value) { collider_velocity = value; }

	// The distance that actually applies, which is the ray's when the sweep did
	// not produce this result.
	float get_distance_to_floor() const { return line_trace ? line_dist : floor_dist; }

	bool is_walkable_floor() const { return blocking_hit && walkable_floor; }

	void copy_from(const Ref<FloorResult>& other);
	void clear();

protected:
	static void _bind_methods();
};

// Everything a mode is handed when it is time to actually move.
//
// A single object rather than six arguments so that a mode written in GDScript
// has one thing to type, and so adding to it later does not break every mode.
class MovementTickParams : public RefCounted
{
	GDCLASS(MovementTickParams, RefCounted)

private:
	CharacterMovementComponent* component;

	// Exactly one of these is set. A mode written for 3D reads the first, one
	// written for 2D the second - Godot keeps the two hierarchies apart all the
	// way down, and a mode cannot be dimension-agnostic when the solver it calls
	// is not.
	CharacterMovementComponent2D* component_2d;

	Ref<MovementState> start_state;
	Ref<MovementInput> input;
	Ref<ProposedMove> proposed_move;
	Ref<MovementState> out_state;
	double delta;

public:
	MovementTickParams();
	~MovementTickParams();

	CharacterMovementComponent* get_component() const { return component; }
	void set_component(CharacterMovementComponent* value) { component = value; }

	CharacterMovementComponent2D* get_component_2d() const { return component_2d; }
	void set_component_2d(CharacterMovementComponent2D* value) { component_2d = value; }

	// The simulation clock of whichever component is driving this tick, so a
	// LayeredMove measuring its own duration does not have to know which
	// dimension it is in.
	double get_sim_time_ms() const;

	Ref<MovementState> get_start_state() const { return start_state; }
	void set_start_state(const Ref<MovementState>& value) { start_state = value; }

	Ref<MovementInput> get_input() const { return input; }
	void set_input(const Ref<MovementInput>& value) { input = value; }

	Ref<ProposedMove> get_proposed_move() const { return proposed_move; }
	void set_proposed_move(const Ref<ProposedMove>& value) { proposed_move = value; }

	// The mode writes its result here. Pre-filled with a copy of start_state, so
	// a mode that changes nothing is still correct.
	Ref<MovementState> get_out_state() const { return out_state; }
	void set_out_state(const Ref<MovementState>& value) { out_state = value; }

	double get_delta() const { return delta; }
	void set_delta(double value) { delta = value; }

protected:
	static void _bind_methods();
};
}

VARIANT_ENUM_CAST(GFGD::ProposedMove::MixMode);

#endif
