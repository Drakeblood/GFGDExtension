#include "movement/modes/falling_mode.h"

#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component.h"
#include "movement/movement_utils.h"

using namespace godot;
using namespace GFGD;

FallingMode::FallingMode()
{
}

FallingMode::~FallingMode()
{
}

void FallingMode::generate_move(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent* component = params->get_component();
	const Ref<MovementState> state = params->get_start_state();
	const Ref<MovementInput> input = params->get_input();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(state.is_null() || input.is_null() || proposed.is_null());

	const Vector3 up = component->get_up_direction();
	const double delta = params->get_delta();
	const Vector3 velocity = state->get_velocity();

	proposed->reset();

	// The two axes are solved separately because they answer to different things:
	// sideways is steering under air control, along up is gravity and nothing
	// else.
	const Vector3 lateral_velocity = velocity.slide(up);
	const Vector3 vertical_velocity = velocity.project(up);

	const Vector3 lateral_acceleration = input->get_move_input().slide(up) * component->get_max_acceleration() * component->get_air_control();

	const Vector3 new_lateral = MovementUtils::compute_velocity(
		lateral_velocity,
		lateral_acceleration,
		component->get_falling_lateral_friction(),
		component->get_braking_deceleration_falling(),
		component->get_braking_friction_factor(),
		component->get_analog_max_speed(input->get_move_input(), component->get_max_walk_speed()),
		delta);

	const Vector3 new_vertical = vertical_velocity - up * (component->get_gravity() * component->get_gravity_scale() * (float)delta);

	proposed->set_linear_velocity(new_lateral + new_vertical);
	proposed->set_direction_intent(input->get_move_input());
	proposed->set_has_dir_intent(!input->get_move_input().is_zero_approx());
	proposed->set_mix_mode(ProposedMove::OVERRIDE_VELOCITY);
}

void FallingMode::simulation_tick(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent* component = params->get_component();
	const Ref<MovementState> start_state = params->get_start_state();
	const Ref<MovementState> out_state = params->get_out_state();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(start_state.is_null() || out_state.is_null() || proposed.is_null());

	const MovementUtils::Body& body = component->get_body();
	if (!body.is_valid())
	{
		return;
	}

	const Vector3 up = component->get_up_direction();
	const double delta = params->get_delta();

	Vector3 velocity = proposed->get_linear_velocity();
	Transform3D transform = Transform3D(start_state->get_rotation(), start_state->get_position());

	const MovementUtils::SlideResult slide = MovementUtils::slide_move(
		body,
		transform,
		velocity * (float)delta,
		velocity,
		up,
		component->get_walkable_floor_cos(),
		component->get_max_slides());

	transform = slide.transform;
	velocity = slide.velocity;

	// Landing is only landing when the character was on the way down. Clipping
	// the top of a ramp while rising is a graze, and treating it as a landing
	// would cut a jump short.
	const bool descending = proposed->get_linear_velocity().dot(up) <= 0.0f;

	if (slide.hit_walkable && descending)
	{
		const Ref<FloorResult> floor = component->get_current_floor();
		MovementUtils::find_floor(
			body,
			transform,
			up,
			component->get_floor_sweep_distance(),
			component->get_floor_sweep_distance(),
			component->get_ground_move_settings(),
			false,
			floor,
			component->get_perch_floor());

		if (floor->is_walkable_floor())
		{
			transform = MovementUtils::adjust_floor_height(body, transform, up, component->get_capsule_radius(), floor);
			velocity = velocity.slide(up);
			component->queue_next_mode(StringName(CharacterMovementComponent::MODE_WALKING));
		}
	}

	out_state->set_position(transform.origin);
	out_state->set_rotation(transform.basis);
	out_state->set_velocity(velocity);
}

void FallingMode::_bind_methods()
{
}
