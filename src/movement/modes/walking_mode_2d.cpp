#include "movement/modes/walking_mode_2d.h"

#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component_2d.h"
#include "movement/movement_utils_2d.h"

using namespace godot;
using namespace GFGD;

// --- WalkingMode2D ----------------------------------------------------------

WalkingMode2D::WalkingMode2D()
{
}

WalkingMode2D::~WalkingMode2D()
{
}

void WalkingMode2D::generate_move(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent2D* component = params->get_component_2d();
	const Ref<MovementState> state = params->get_start_state();
	const Ref<MovementInput> input = params->get_input();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(state.is_null() || input.is_null() || proposed.is_null());

	proposed->reset();

	const Vector2 up = component->get_up_direction();
	const Vector2 move_input = MovementUtils2D::to_2d(input->get_move_input());

	// Flat against the floor plane: whatever vertical velocity survived the
	// landing is not what the player asked for.
	const Vector2 current_velocity = MovementUtils2D::to_2d(state->get_velocity()).slide(up);
	const Vector2 acceleration = move_input.slide(up) * component->get_max_acceleration();

	const Vector2 new_velocity = MovementUtils2D::compute_velocity(
		current_velocity,
		acceleration,
		component->get_ground_friction(),
		component->get_braking_deceleration_walking(),
		component->get_braking_friction_factor(),
		component->get_analog_max_speed(move_input, component->get_effective_max_walk_speed()),
		params->get_delta());

	proposed->set_linear_velocity(MovementUtils2D::to_3d(new_velocity));
	proposed->set_direction_intent(input->get_move_input());
	proposed->set_has_dir_intent(!move_input.is_zero_approx());
	proposed->set_mix_mode(ProposedMove::OVERRIDE_VELOCITY);
}

void WalkingMode2D::simulation_tick(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent2D* component = params->get_component_2d();
	const Ref<MovementState> start_state = params->get_start_state();
	const Ref<MovementState> out_state = params->get_out_state();
	const Ref<MovementInput> input = params->get_input();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(start_state.is_null() || out_state.is_null() || input.is_null() || proposed.is_null());

	const MovementUtils2D::Body& body = component->get_body();
	if (!body.is_valid())
	{
		return;
	}

	const Vector2 up = component->get_up_direction();
	const double delta = params->get_delta();
	Vector2 velocity = MovementUtils2D::to_2d(proposed->get_linear_velocity());

	Transform2D transform(0.0, MovementUtils2D::to_2d(start_state->get_position()));

	const Ref<FloorResult> floor = component->get_current_floor();

	// Captured before the post-move floor test overwrites it. Leaving a platform -
	// by jumping or by walking off the end - has to hand back the platform's own
	// motion, and by then the floor result no longer describes the platform.
	const Vector2 base_velocity = component->get_impart_base_velocity()
		? MovementUtils2D::to_2d(floor->get_collider_velocity())
		: Vector2();

	if (input->get_want_jump() && component->get_jump_velocity() > 0.0f)
	{
		velocity += up * component->get_jump_velocity();
		velocity += base_velocity;

		out_state->set_position(MovementUtils2D::to_3d(transform.get_origin()));
		out_state->set_velocity(MovementUtils2D::to_3d(velocity));

		component->queue_next_mode(StringName(CharacterMovementComponent2D::MODE_FALLING));
		return;
	}

	const MovementUtils2D::GroundMoveSettings settings = component->get_ground_move_settings();

	Vector2 move_delta = velocity * (float)delta;
	if (floor.is_valid() && floor->is_walkable_floor() && !floor->get_line_trace())
	{
		move_delta = MovementUtils2D::compute_ground_movement_delta(move_delta, MovementUtils2D::to_2d(floor->get_normal()), up);
	}

	const MovementUtils2D::SlideResult slide = MovementUtils2D::move_along_floor(body, transform, move_delta, velocity, up, floor, settings);

	transform = slide.transform;
	velocity = slide.velocity;

	MovementUtils2D::find_floor(body, transform, up, component->get_floor_sweep_distance(), component->get_floor_sweep_distance(), settings, floor);

	if (floor->is_walkable_floor())
	{
		transform = MovementUtils2D::adjust_floor_height(body, transform, up, floor);
		velocity = velocity.slide(up);
	}
	else
	{
		velocity += base_velocity;
		component->queue_next_mode(StringName(CharacterMovementComponent2D::MODE_FALLING));
	}

	out_state->set_position(MovementUtils2D::to_3d(transform.get_origin()));
	out_state->set_velocity(MovementUtils2D::to_3d(velocity));
}

void WalkingMode2D::_bind_methods()
{
}

// --- FallingMode2D ----------------------------------------------------------

FallingMode2D::FallingMode2D()
{
}

FallingMode2D::~FallingMode2D()
{
}

void FallingMode2D::generate_move(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent2D* component = params->get_component_2d();
	const Ref<MovementState> state = params->get_start_state();
	const Ref<MovementInput> input = params->get_input();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(state.is_null() || input.is_null() || proposed.is_null());

	proposed->reset();

	const Vector2 up = component->get_up_direction();
	const double delta = params->get_delta();
	const Vector2 velocity = MovementUtils2D::to_2d(state->get_velocity());
	const Vector2 move_input = MovementUtils2D::to_2d(input->get_move_input());

	const Vector2 lateral_velocity = velocity.slide(up);
	const Vector2 vertical_velocity = up * velocity.dot(up);

	const Vector2 lateral_acceleration = move_input.slide(up) * component->get_max_acceleration() * component->get_air_control();

	const Vector2 new_lateral = MovementUtils2D::compute_velocity(
		lateral_velocity,
		lateral_acceleration,
		component->get_falling_lateral_friction(),
		component->get_braking_deceleration_falling(),
		component->get_braking_friction_factor(),
		component->get_analog_max_speed(move_input, component->get_max_walk_speed()),
		delta);

	const Vector2 new_vertical = vertical_velocity - up * (component->get_gravity() * component->get_gravity_scale() * (float)delta);

	proposed->set_linear_velocity(MovementUtils2D::to_3d(new_lateral + new_vertical));
	proposed->set_direction_intent(input->get_move_input());
	proposed->set_has_dir_intent(!move_input.is_zero_approx());
	proposed->set_mix_mode(ProposedMove::OVERRIDE_VELOCITY);
}

void FallingMode2D::simulation_tick(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent2D* component = params->get_component_2d();
	const Ref<MovementState> start_state = params->get_start_state();
	const Ref<MovementState> out_state = params->get_out_state();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(start_state.is_null() || out_state.is_null() || proposed.is_null());

	const MovementUtils2D::Body& body = component->get_body();
	if (!body.is_valid())
	{
		return;
	}

	const Vector2 up = component->get_up_direction();
	Vector2 velocity = MovementUtils2D::to_2d(proposed->get_linear_velocity());
	Transform2D transform(0.0, MovementUtils2D::to_2d(start_state->get_position()));

	const MovementUtils2D::SlideResult slide = MovementUtils2D::slide_move(
		body, transform, velocity * (float)params->get_delta(), velocity, up,
		component->get_walkable_floor_cos(), component->get_max_slides());

	transform = slide.transform;
	velocity = slide.velocity;

	// Landing is only landing on the way down; clipping a ledge while rising is
	// a graze and would cut a jump short.
	const bool descending = MovementUtils2D::to_2d(proposed->get_linear_velocity()).dot(up) <= 0.0f;

	if (slide.hit_walkable && descending)
	{
		const Ref<FloorResult> floor = component->get_current_floor();
		const MovementUtils2D::GroundMoveSettings settings = component->get_ground_move_settings();

		MovementUtils2D::find_floor(body, transform, up, component->get_floor_sweep_distance(), component->get_floor_sweep_distance(), settings, floor);

		if (floor->is_walkable_floor())
		{
			transform = MovementUtils2D::adjust_floor_height(body, transform, up, floor);
			velocity = velocity.slide(up);
			component->queue_next_mode(StringName(CharacterMovementComponent2D::MODE_WALKING));
		}
	}

	out_state->set_position(MovementUtils2D::to_3d(transform.get_origin()));
	out_state->set_velocity(MovementUtils2D::to_3d(velocity));
}

void FallingMode2D::_bind_methods()
{
}

// --- FlyingMode2D -----------------------------------------------------------

FlyingMode2D::FlyingMode2D()
{
}

FlyingMode2D::~FlyingMode2D()
{
}

void FlyingMode2D::generate_move(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent2D* component = params->get_component_2d();
	const Ref<MovementState> state = params->get_start_state();
	const Ref<MovementInput> input = params->get_input();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(state.is_null() || input.is_null() || proposed.is_null());

	proposed->reset();

	// Nothing is flattened: the whole difference from walking is that the input
	// keeps whatever vertical part it arrived with.
	const Vector2 move_input = MovementUtils2D::to_2d(input->get_move_input());
	const Vector2 acceleration = move_input * component->get_max_acceleration();

	const Vector2 new_velocity = MovementUtils2D::compute_velocity(
		MovementUtils2D::to_2d(state->get_velocity()),
		acceleration,
		component->get_ground_friction(),
		component->get_braking_deceleration_flying(),
		component->get_braking_friction_factor(),
		component->get_analog_max_speed(move_input, component->get_max_fly_speed()),
		params->get_delta());

	proposed->set_linear_velocity(MovementUtils2D::to_3d(new_velocity));
	proposed->set_direction_intent(input->get_move_input());
	proposed->set_has_dir_intent(!move_input.is_zero_approx());
	proposed->set_mix_mode(ProposedMove::OVERRIDE_VELOCITY);
}

void FlyingMode2D::simulation_tick(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent2D* component = params->get_component_2d();
	const Ref<MovementState> start_state = params->get_start_state();
	const Ref<MovementState> out_state = params->get_out_state();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(start_state.is_null() || out_state.is_null() || proposed.is_null());

	const MovementUtils2D::Body& body = component->get_body();
	if (!body.is_valid())
	{
		return;
	}

	const Vector2 velocity = MovementUtils2D::to_2d(proposed->get_linear_velocity());
	const Transform2D transform(0.0, MovementUtils2D::to_2d(start_state->get_position()));

	// A plain slide, with no floor test at all: ground under a flying character
	// is not a landing, and saying so is a transition's job.
	const MovementUtils2D::SlideResult slide = MovementUtils2D::slide_move(
		body, transform, velocity * (float)params->get_delta(), velocity,
		component->get_up_direction(), component->get_walkable_floor_cos(),
		component->get_max_slides());

	out_state->set_position(MovementUtils2D::to_3d(slide.transform.get_origin()));
	out_state->set_velocity(MovementUtils2D::to_3d(slide.velocity));
	out_state->clear_base();
}

void FlyingMode2D::_bind_methods()
{
}
