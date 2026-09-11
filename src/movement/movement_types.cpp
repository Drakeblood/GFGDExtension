#include "movement/movement_types.h"

#include <godot_cpp/core/class_db.hpp>

// MovementTickParams hands out a CharacterMovementComponent*, and binding that
// getter needs the complete type. The header only forward-declares it, which is
// what keeps the two files from including each other.
#include "movement/character_movement_component.h"
#include "movement/character_movement_component_2d.h"

using namespace godot;
using namespace GFGD;

// --- MovementInput ----------------------------------------------------------

MovementInput::MovementInput()
	: move_input(Vector3())
	, want_jump(false)
	, want_crouch(false)
	, frame(0)
{
}

MovementInput::~MovementInput()
{
}

void MovementInput::reset()
{
	move_input = Vector3();
	want_jump = false;
	want_crouch = false;
	frame = 0;
}

void MovementInput::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_move_input"), &MovementInput::get_move_input);
	ClassDB::bind_method(D_METHOD("set_move_input", "value"), &MovementInput::set_move_input);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "move_input"), "set_move_input", "get_move_input");

	ClassDB::bind_method(D_METHOD("get_want_jump"), &MovementInput::get_want_jump);
	ClassDB::bind_method(D_METHOD("set_want_jump", "value"), &MovementInput::set_want_jump);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "want_jump"), "set_want_jump", "get_want_jump");

	ClassDB::bind_method(D_METHOD("get_want_crouch"), &MovementInput::get_want_crouch);
	ClassDB::bind_method(D_METHOD("set_want_crouch", "value"), &MovementInput::set_want_crouch);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "want_crouch"), "set_want_crouch", "get_want_crouch");

	ClassDB::bind_method(D_METHOD("get_frame"), &MovementInput::get_frame);
	ClassDB::bind_method(D_METHOD("set_frame", "value"), &MovementInput::set_frame);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frame"), "set_frame", "get_frame");

	ClassDB::bind_method(D_METHOD("reset"), &MovementInput::reset);
}

// --- MovementState ----------------------------------------------------------

MovementState::MovementState()
	: position(Vector3())
	, rotation(Basis())
	, velocity(Vector3())
	, movement_mode(StringName())
	, is_crouching(false)
	, base_id(0)
	, base_relative_position(Vector3())
	, base_relative_rotation(Basis())
{
}

MovementState::~MovementState()
{
}

void MovementState::copy_from(const Ref<MovementState>& other)
{
	ERR_FAIL_COND_MSG(other.is_null(), "GFGD: MovementState.copy_from was given nothing to copy.");

	position = other->position;
	rotation = other->rotation;
	velocity = other->velocity;
	movement_mode = other->movement_mode;
	is_crouching = other->is_crouching;
	base_id = other->base_id;
	base_relative_position = other->base_relative_position;
	base_relative_rotation = other->base_relative_rotation;

	// Skipped entirely when both sides are empty, which is the overwhelmingly
	// common case and the reason this can afford to be here at all - copy_from
	// runs twice per substep.
	if (!queued_layered_moves.is_empty() || !other->queued_layered_moves.is_empty())
	{
		queued_layered_moves = other->queued_layered_moves.duplicate(false);
	}
	if (!active_layered_moves.is_empty() || !other->active_layered_moves.is_empty())
	{
		active_layered_moves = other->active_layered_moves.duplicate(false);
	}

	// A shallow assignment would leave both states sharing one Dictionary, so a
	// mode writing into the working copy would reach back into the state a
	// rollback is supposed to restore.
	extra = other->extra.duplicate(true);
}

Ref<MovementState> MovementState::duplicate_state() const
{
	Ref<MovementState> copy;
	copy.instantiate();
	copy->position = position;
	copy->rotation = rotation;
	copy->velocity = velocity;
	copy->movement_mode = movement_mode;
	copy->is_crouching = is_crouching;
	copy->base_id = base_id;
	copy->base_relative_position = base_relative_position;
	copy->base_relative_rotation = base_relative_rotation;
	copy->queued_layered_moves = queued_layered_moves.duplicate(false);
	copy->active_layered_moves = active_layered_moves.duplicate(false);
	copy->extra = extra.duplicate(true);
	return copy;
}

void MovementState::clear_base()
{
	base_id = 0;
	base_relative_position = Vector3();
	base_relative_rotation = Basis();
}

bool MovementState::should_reconcile(const Ref<MovementState>& authority_state, float position_tolerance, float velocity_tolerance) const
{
	if (authority_state.is_null())
	{
		return false;
	}

	// A mode change is never within tolerance: the next tick would run different
	// code, so the divergence only grows.
	if (movement_mode != authority_state->movement_mode)
	{
		return true;
	}

	if (position.distance_squared_to(authority_state->position) > position_tolerance * position_tolerance)
	{
		return true;
	}

	if (velocity.distance_squared_to(authority_state->velocity) > velocity_tolerance * velocity_tolerance)
	{
		return true;
	}

	return false;
}

void MovementState::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_position"), &MovementState::get_position);
	ClassDB::bind_method(D_METHOD("set_position", "value"), &MovementState::set_position);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "position"), "set_position", "get_position");

	ClassDB::bind_method(D_METHOD("get_rotation"), &MovementState::get_rotation);
	ClassDB::bind_method(D_METHOD("set_rotation", "value"), &MovementState::set_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BASIS, "rotation"), "set_rotation", "get_rotation");

	ClassDB::bind_method(D_METHOD("get_velocity"), &MovementState::get_velocity);
	ClassDB::bind_method(D_METHOD("set_velocity", "value"), &MovementState::set_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "velocity"), "set_velocity", "get_velocity");

	ClassDB::bind_method(D_METHOD("get_movement_mode"), &MovementState::get_movement_mode);
	ClassDB::bind_method(D_METHOD("set_movement_mode", "value"), &MovementState::set_movement_mode);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "movement_mode"), "set_movement_mode", "get_movement_mode");

	ClassDB::bind_method(D_METHOD("get_extra"), &MovementState::get_extra);
	ClassDB::bind_method(D_METHOD("set_extra", "value"), &MovementState::set_extra);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "extra"), "set_extra", "get_extra");

	ClassDB::bind_method(D_METHOD("get_is_crouching"), &MovementState::get_is_crouching);
	ClassDB::bind_method(D_METHOD("set_is_crouching", "value"), &MovementState::set_is_crouching);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_crouching"), "set_is_crouching", "get_is_crouching");

	ClassDB::bind_method(D_METHOD("get_base_id"), &MovementState::get_base_id);
	ClassDB::bind_method(D_METHOD("set_base_id", "value"), &MovementState::set_base_id);

	ClassDB::bind_method(D_METHOD("get_base_relative_position"), &MovementState::get_base_relative_position);
	ClassDB::bind_method(D_METHOD("set_base_relative_position", "value"), &MovementState::set_base_relative_position);

	ClassDB::bind_method(D_METHOD("get_base_relative_rotation"), &MovementState::get_base_relative_rotation);
	ClassDB::bind_method(D_METHOD("set_base_relative_rotation", "value"), &MovementState::set_base_relative_rotation);

	ClassDB::bind_method(D_METHOD("get_queued_layered_moves"), &MovementState::get_queued_layered_moves);
	ClassDB::bind_method(D_METHOD("set_queued_layered_moves", "value"), &MovementState::set_queued_layered_moves);

	ClassDB::bind_method(D_METHOD("get_active_layered_moves"), &MovementState::get_active_layered_moves);
	ClassDB::bind_method(D_METHOD("set_active_layered_moves", "value"), &MovementState::set_active_layered_moves);

	ClassDB::bind_method(D_METHOD("has_base"), &MovementState::has_base);
	ClassDB::bind_method(D_METHOD("clear_base"), &MovementState::clear_base);

	ClassDB::bind_method(D_METHOD("copy_from", "other"), &MovementState::copy_from);
	ClassDB::bind_method(D_METHOD("duplicate_state"), &MovementState::duplicate_state);
	ClassDB::bind_method(D_METHOD("should_reconcile", "authority_state", "position_tolerance", "velocity_tolerance"), &MovementState::should_reconcile, DEFVAL(0.05f), DEFVAL(0.5f));
}

// --- ProposedMove -----------------------------------------------------------

ProposedMove::ProposedMove()
	: preferred_mode(StringName())
	, direction_intent(Vector3())
	, linear_velocity(Vector3())
	, angular_velocity_deg(Vector3())
	, has_dir_intent(false)
	, mix_mode(OVERRIDE_VELOCITY)
{
}

ProposedMove::~ProposedMove()
{
}

void ProposedMove::reset()
{
	preferred_mode = StringName();
	direction_intent = Vector3();
	linear_velocity = Vector3();
	angular_velocity_deg = Vector3();
	has_dir_intent = false;
	mix_mode = OVERRIDE_VELOCITY;
}

void ProposedMove::_bind_methods()
{
	BIND_ENUM_CONSTANT(ADDITIVE_VELOCITY);
	BIND_ENUM_CONSTANT(OVERRIDE_VELOCITY);
	BIND_ENUM_CONSTANT(OVERRIDE_ALL);
	BIND_ENUM_CONSTANT(OVERRIDE_ALL_EXCEPT_VERTICAL);

	ClassDB::bind_method(D_METHOD("get_preferred_mode"), &ProposedMove::get_preferred_mode);
	ClassDB::bind_method(D_METHOD("set_preferred_mode", "value"), &ProposedMove::set_preferred_mode);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "preferred_mode"), "set_preferred_mode", "get_preferred_mode");

	ClassDB::bind_method(D_METHOD("get_direction_intent"), &ProposedMove::get_direction_intent);
	ClassDB::bind_method(D_METHOD("set_direction_intent", "value"), &ProposedMove::set_direction_intent);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "direction_intent"), "set_direction_intent", "get_direction_intent");

	ClassDB::bind_method(D_METHOD("get_linear_velocity"), &ProposedMove::get_linear_velocity);
	ClassDB::bind_method(D_METHOD("set_linear_velocity", "value"), &ProposedMove::set_linear_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "linear_velocity"), "set_linear_velocity", "get_linear_velocity");

	ClassDB::bind_method(D_METHOD("get_angular_velocity_deg"), &ProposedMove::get_angular_velocity_deg);
	ClassDB::bind_method(D_METHOD("set_angular_velocity_deg", "value"), &ProposedMove::set_angular_velocity_deg);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "angular_velocity_deg"), "set_angular_velocity_deg", "get_angular_velocity_deg");

	ClassDB::bind_method(D_METHOD("get_has_dir_intent"), &ProposedMove::get_has_dir_intent);
	ClassDB::bind_method(D_METHOD("set_has_dir_intent", "value"), &ProposedMove::set_has_dir_intent);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_dir_intent"), "set_has_dir_intent", "get_has_dir_intent");

	ClassDB::bind_method(D_METHOD("get_mix_mode"), &ProposedMove::get_mix_mode);
	ClassDB::bind_method(D_METHOD("set_mix_mode", "value"), &ProposedMove::set_mix_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mix_mode", PROPERTY_HINT_ENUM, "Additive Velocity,Override Velocity,Override All,Override All Except Vertical"), "set_mix_mode", "get_mix_mode");

	ClassDB::bind_method(D_METHOD("reset"), &ProposedMove::reset);
}

// --- FloorResult ------------------------------------------------------------

FloorResult::FloorResult()
	: blocking_hit(false)
	, walkable_floor(false)
	, line_trace(false)
	, floor_dist(0.0f)
	, line_dist(0.0f)
	, normal(Vector3(0, 1, 0))
	, point(Vector3())
	, collider(nullptr)
	, collider_velocity(Vector3())
{
}

FloorResult::~FloorResult()
{
}

void FloorResult::copy_from(const Ref<FloorResult>& other)
{
	ERR_FAIL_COND_MSG(other.is_null(), "GFGD: FloorResult.copy_from was given nothing to copy.");

	blocking_hit = other->blocking_hit;
	walkable_floor = other->walkable_floor;
	line_trace = other->line_trace;
	floor_dist = other->floor_dist;
	line_dist = other->line_dist;
	normal = other->normal;
	point = other->point;
	collider = other->collider;
	collider_velocity = other->collider_velocity;
}

void FloorResult::clear()
{
	blocking_hit = false;
	walkable_floor = false;
	line_trace = false;
	floor_dist = 0.0f;
	line_dist = 0.0f;
	normal = Vector3(0, 1, 0);
	point = Vector3();
	collider = nullptr;
	collider_velocity = Vector3();
}

void FloorResult::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_blocking_hit"), &FloorResult::get_blocking_hit);
	ClassDB::bind_method(D_METHOD("set_blocking_hit", "value"), &FloorResult::set_blocking_hit);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "blocking_hit"), "set_blocking_hit", "get_blocking_hit");

	ClassDB::bind_method(D_METHOD("get_walkable_floor"), &FloorResult::get_walkable_floor);
	ClassDB::bind_method(D_METHOD("set_walkable_floor", "value"), &FloorResult::set_walkable_floor);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "walkable_floor"), "set_walkable_floor", "get_walkable_floor");

	ClassDB::bind_method(D_METHOD("get_line_trace"), &FloorResult::get_line_trace);
	ClassDB::bind_method(D_METHOD("set_line_trace", "value"), &FloorResult::set_line_trace);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "line_trace"), "set_line_trace", "get_line_trace");

	ClassDB::bind_method(D_METHOD("get_floor_dist"), &FloorResult::get_floor_dist);
	ClassDB::bind_method(D_METHOD("set_floor_dist", "value"), &FloorResult::set_floor_dist);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_dist", PROPERTY_HINT_NONE, "suffix:m"), "set_floor_dist", "get_floor_dist");

	ClassDB::bind_method(D_METHOD("get_line_dist"), &FloorResult::get_line_dist);
	ClassDB::bind_method(D_METHOD("set_line_dist", "value"), &FloorResult::set_line_dist);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "line_dist", PROPERTY_HINT_NONE, "suffix:m"), "set_line_dist", "get_line_dist");

	ClassDB::bind_method(D_METHOD("get_normal"), &FloorResult::get_normal);
	ClassDB::bind_method(D_METHOD("set_normal", "value"), &FloorResult::set_normal);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "normal"), "set_normal", "get_normal");

	ClassDB::bind_method(D_METHOD("get_point"), &FloorResult::get_point);
	ClassDB::bind_method(D_METHOD("set_point", "value"), &FloorResult::set_point);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "point"), "set_point", "get_point");

	ClassDB::bind_method(D_METHOD("get_collider"), &FloorResult::get_collider);
	ClassDB::bind_method(D_METHOD("set_collider", "value"), &FloorResult::set_collider);

	ClassDB::bind_method(D_METHOD("get_collider_velocity"), &FloorResult::get_collider_velocity);
	ClassDB::bind_method(D_METHOD("set_collider_velocity", "value"), &FloorResult::set_collider_velocity);

	ClassDB::bind_method(D_METHOD("get_distance_to_floor"), &FloorResult::get_distance_to_floor);
	ClassDB::bind_method(D_METHOD("is_walkable_floor"), &FloorResult::is_walkable_floor);
	ClassDB::bind_method(D_METHOD("copy_from", "other"), &FloorResult::copy_from);
	ClassDB::bind_method(D_METHOD("clear"), &FloorResult::clear);
}

// --- MovementTickParams -----------------------------------------------------

MovementTickParams::MovementTickParams()
	: component(nullptr)
	, component_2d(nullptr)
	, delta(0.0)
{
}

MovementTickParams::~MovementTickParams()
{
}

double MovementTickParams::get_sim_time_ms() const
{
	if (component != nullptr)
	{
		return component->get_sim_time_ms();
	}

	if (component_2d != nullptr)
	{
		return component_2d->get_sim_time_ms();
	}

	return 0.0;
}

void MovementTickParams::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_component"), &MovementTickParams::get_component);
	ClassDB::bind_method(D_METHOD("set_component", "value"), &MovementTickParams::set_component);

	ClassDB::bind_method(D_METHOD("get_component_2d"), &MovementTickParams::get_component_2d);
	ClassDB::bind_method(D_METHOD("set_component_2d", "value"), &MovementTickParams::set_component_2d);

	ClassDB::bind_method(D_METHOD("get_start_state"), &MovementTickParams::get_start_state);
	ClassDB::bind_method(D_METHOD("set_start_state", "value"), &MovementTickParams::set_start_state);

	ClassDB::bind_method(D_METHOD("get_input"), &MovementTickParams::get_input);
	ClassDB::bind_method(D_METHOD("set_input", "value"), &MovementTickParams::set_input);

	ClassDB::bind_method(D_METHOD("get_proposed_move"), &MovementTickParams::get_proposed_move);
	ClassDB::bind_method(D_METHOD("set_proposed_move", "value"), &MovementTickParams::set_proposed_move);

	ClassDB::bind_method(D_METHOD("get_out_state"), &MovementTickParams::get_out_state);
	ClassDB::bind_method(D_METHOD("set_out_state", "value"), &MovementTickParams::set_out_state);

	ClassDB::bind_method(D_METHOD("get_sim_time_ms"), &MovementTickParams::get_sim_time_ms);
	ClassDB::bind_method(D_METHOD("get_delta"), &MovementTickParams::get_delta);
	ClassDB::bind_method(D_METHOD("set_delta", "value"), &MovementTickParams::set_delta);
}
