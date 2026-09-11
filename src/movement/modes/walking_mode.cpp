#include "movement/modes/walking_mode.h"

#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component.h"
#include "movement/movement_utils.h"

using namespace godot;
using namespace GFGD;

WalkingMode::WalkingMode()
{
}

WalkingMode::~WalkingMode()
{
}

void WalkingMode::generate_move(const Ref<MovementTickParams>& params)
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

	proposed->reset();

	// Walking is a flat problem: whatever vertical velocity survived the landing
	// is not what the player asked for, and carrying it would let a character
	// accumulate downward speed while standing still.
	const Vector3 current_velocity = state->get_velocity().slide(up);
	const Vector3 acceleration = input->get_move_input().slide(up) * component->get_max_acceleration();

	const Vector3 new_velocity = MovementUtils::compute_velocity(
		current_velocity,
		acceleration,
		component->get_ground_friction(),
		component->get_braking_deceleration_walking(),
		component->get_braking_friction_factor(),
		component->get_analog_max_speed(input->get_move_input(), component->get_effective_max_walk_speed()),
		delta);

	proposed->set_linear_velocity(new_velocity);
	proposed->set_direction_intent(input->get_move_input());
	proposed->set_has_dir_intent(!input->get_move_input().is_zero_approx());
	proposed->set_mix_mode(ProposedMove::OVERRIDE_VELOCITY);
}

void WalkingMode::simulation_tick(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent* component = params->get_component();
	const Ref<MovementState> start_state = params->get_start_state();
	const Ref<MovementState> out_state = params->get_out_state();
	const Ref<MovementInput> input = params->get_input();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(start_state.is_null() || out_state.is_null() || input.is_null() || proposed.is_null());

	const MovementUtils::Body& body = component->get_body();
	if (!body.is_valid())
	{
		return;
	}

	const Vector3 up = component->get_up_direction();
	const double delta = params->get_delta();
	Vector3 velocity = proposed->get_linear_velocity();

	Transform3D transform = Transform3D(start_state->get_rotation(), start_state->get_position());

	const Ref<FloorResult> floor = component->get_current_floor();

	// Captured before the post-move floor test overwrites it. Leaving a platform -
	// by jumping or by walking off the end - has to hand back the platform's own
	// motion, and by then the floor result no longer describes the platform.
	const Vector3 base_velocity = component->get_impart_base_velocity() ? floor->get_collider_velocity() : Vector3();

	// A jump leaves the ground before anything else happens this tick, so the
	// upward velocity is not immediately flattened by the walking solver.
	if (input->get_want_jump() && component->get_jump_velocity() > 0.0f)
	{
		velocity += up * component->get_jump_velocity();
		velocity += base_velocity;

		out_state->set_position(transform.origin);
		out_state->set_rotation(transform.basis);
		out_state->set_velocity(velocity);

		component->queue_next_mode(StringName(CharacterMovementComponent::MODE_FALLING));
		return;
	}

	Vector3 move_delta = velocity * (float)delta;
	if (floor.is_valid() && floor->is_walkable_floor() && !floor->get_line_trace())
	{
		move_delta = MovementUtils::compute_ground_movement_delta(move_delta, floor->get_normal(), up);
	}

	// move_along_floor rather than slide_move: on the ground, something too steep
	// to walk on might still be short enough to walk over, and that check has to
	// happen before the move is turned into a slide.
	const MovementUtils::GroundMoveSettings settings = component->get_ground_move_settings();

	const MovementUtils::SlideResult slide = MovementUtils::move_along_floor(
		body,
		transform,
		move_delta,
		velocity,
		up,
		floor,
		settings);

	transform = slide.transform;
	velocity = slide.velocity;

	// Where the character ended up decides what happens next, so the floor is
	// looked for again rather than reused from before the move. This overwrites
	// the component's cached floor, which is the point - the next tick's ramp
	// calculation wants the surface actually under the character now.
	MovementUtils::find_floor(
		body,
		transform,
		up,
		component->get_floor_sweep_distance(),
		component->get_floor_sweep_distance(),
		settings,
		true,
		floor,
		component->get_perch_floor());

	if (floor->is_walkable_floor())
	{
		transform = MovementUtils::adjust_floor_height(body, transform, up, component->get_capsule_radius(), floor);

		// Standing on ground means no vertical velocity, whatever the slide left
		// behind. Keeping it would have gravity quietly build up while walking.
		velocity = velocity.slide(up);
	}
	else if (!settings.can_walk_off_ledges)
	{
		// Stop at the edge rather than step off it. Unreal works out how far along
		// the move it could still have gone; this gives the whole move back, which
		// is blunter but keeps the promise the flag makes - the character never
		// falls off on its own.
		transform = Transform3D(start_state->get_rotation(), start_state->get_position());
		velocity = velocity.slide(up);

		MovementUtils::find_floor(
			body,
			transform,
			up,
			component->get_floor_sweep_distance(),
			component->get_floor_sweep_distance(),
			settings,
			true,
			floor,
			component->get_perch_floor());

		// Without this the character sinks a little each time it is held back at
		// an edge, because the branch that normally keeps it in the floor band is
		// the one being skipped.
		transform = MovementUtils::adjust_floor_height(body, transform, up, component->get_capsule_radius(), floor);
	}
	else
	{
		velocity += base_velocity;
		component->queue_next_mode(StringName(CharacterMovementComponent::MODE_FALLING));
	}

	out_state->set_position(transform.origin);
	out_state->set_rotation(transform.basis);
	out_state->set_velocity(velocity);
}

void WalkingMode::_bind_methods()
{
}
