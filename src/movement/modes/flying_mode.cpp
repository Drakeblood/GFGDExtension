#include "movement/modes/flying_mode.h"

#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component.h"
#include "movement/movement_utils.h"

using namespace godot;
using namespace GFGD;

FlyingMode::FlyingMode()
{
}

FlyingMode::~FlyingMode()
{
}

void FlyingMode::generate_move(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent* component = params->get_component();
	const Ref<MovementState> state = params->get_start_state();
	const Ref<MovementInput> input = params->get_input();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(state.is_null() || input.is_null() || proposed.is_null());

	proposed->reset();

	// Nothing is flattened here. The whole difference between this and walking is
	// that the input keeps whatever vertical part it arrived with.
	const Vector3 acceleration = input->get_move_input() * component->get_max_acceleration();

	const Vector3 new_velocity = MovementUtils::compute_velocity(
		state->get_velocity(),
		acceleration,
		component->get_ground_friction(),
		component->get_braking_deceleration_flying(),
		component->get_braking_friction_factor(),
		component->get_analog_max_speed(input->get_move_input(), component->get_max_fly_speed()),
		params->get_delta());

	proposed->set_linear_velocity(new_velocity);
	proposed->set_direction_intent(input->get_move_input());
	proposed->set_has_dir_intent(!input->get_move_input().is_zero_approx());
	proposed->set_mix_mode(ProposedMove::OVERRIDE_VELOCITY);
}

void FlyingMode::simulation_tick(const Ref<MovementTickParams>& params)
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
	const Vector3 velocity = proposed->get_linear_velocity();
	const Transform3D transform = Transform3D(start_state->get_rotation(), start_state->get_position());

	// A plain slide, with no floor test at all. A flying character that finds
	// ground under it has not landed; something else has to decide that, which is
	// what a transition is for.
	const MovementUtils::SlideResult slide = MovementUtils::slide_move(
		body,
		transform,
		velocity * (float)params->get_delta(),
		velocity,
		up,
		component->get_walkable_floor_cos(),
		component->get_max_slides());

	out_state->set_position(slide.transform.origin);
	out_state->set_rotation(slide.transform.basis);
	out_state->set_velocity(slide.velocity);

	// Nothing is being stood on while flying, and a base left behind would drag
	// the character along with a platform it is no longer touching.
	out_state->clear_base();
}

void FlyingMode::_bind_methods()
{
}
