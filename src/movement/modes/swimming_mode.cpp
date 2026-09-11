#include "movement/modes/swimming_mode.h"

#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component.h"
#include "movement/movement_utils.h"
#include "movement/water_volume.h"

using namespace godot;
using namespace GFGD;

SwimmingMode::SwimmingMode()
{
}

SwimmingMode::~SwimmingMode()
{
}

void SwimmingMode::generate_move(const Ref<MovementTickParams>& params)
{
	ERR_FAIL_COND(params.is_null());

	CharacterMovementComponent* component = params->get_component();
	const Ref<MovementState> state = params->get_start_state();
	const Ref<MovementInput> input = params->get_input();
	const Ref<ProposedMove> proposed = params->get_proposed_move();

	ERR_FAIL_NULL(component);
	ERR_FAIL_COND(state.is_null() || input.is_null() || proposed.is_null());

	proposed->reset();

	const Vector3 up = component->get_up_direction();
	const double delta = params->get_delta();
	const Transform3D transform(state->get_rotation(), state->get_position());

	const float immersion = component->get_immersion_depth_at(transform);
	const WaterVolume* water = Object::cast_to<WaterVolume>(component->find_water_volume_at(transform.origin));

	const float buoyancy = water != nullptr ? water->get_buoyancy() : 1.0f;
	const Vector3 current = water != nullptr ? water->get_water_velocity() : Vector3();

	Vector3 acceleration = input->get_move_input() * component->get_max_acceleration();

	// Nothing to push against above the surface. The part of the stroke that
	// lifts is scaled by how much of the character the water still holds, so a
	// character holding "up" treads water at the waterline instead of swimming
	// clean out of the pool, arcing through the air and dropping back in. It did
	// exactly that before: the mode stays swimming until the feet clear, and a
	// stroke of 20.48 m/s² out-accelerates gravity two to one, so the capsule
	// left the water under power. Getting out is the jump's job.
	const float lift = acceleration.dot(up);
	if (lift > 0.0f)
	{
		acceleration -= up * (lift * (1.0f - immersion));
	}

	Vector3 velocity = MovementUtils::compute_velocity(
		state->get_velocity() - current,
		acceleration,
		component->get_ground_friction(),
		component->get_braking_deceleration_swimming(),
		component->get_braking_friction_factor(),
		component->get_analog_max_speed(input->get_move_input(), component->get_max_swim_speed()),
		delta);

	// Gravity scaled by how much of the character the water is holding up. At
	// neutral buoyancy a fully submerged character feels none of it, and one at
	// the surface feels half - which is what sinks it until it is under, and then
	// leaves it hanging there.
	const float net_buoyancy = buoyancy * immersion;
	velocity -= up * (component->get_gravity() * component->get_gravity_scale() * (1.0f - net_buoyancy) * (float)delta);

	// Rising is damped by how little water is left holding the character up, so
	// swimming hard at the surface floats rather than launching. Without it a
	// character porpoises: out of the water, an arc through the air, back in, over
	// and over, with the mode flipping each time.
	const float max_swim_speed = component->get_max_swim_speed();
	const float rising = velocity.dot(up);
	if (net_buoyancy != 0.0f && rising > 0.33f * max_swim_speed)
	{
		const float damped = MAX(0.33f * max_swim_speed, rising * immersion * immersion);
		velocity += up * (damped - rising);
	}

	proposed->set_linear_velocity(velocity + current);
	proposed->set_direction_intent(input->get_move_input());
	proposed->set_has_dir_intent(!input->get_move_input().is_zero_approx());
	proposed->set_mix_mode(ProposedMove::OVERRIDE_VELOCITY);
}

void SwimmingMode::simulation_tick(const Ref<MovementTickParams>& params)
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
	Vector3 velocity = proposed->get_linear_velocity();
	const Transform3D transform(start_state->get_rotation(), start_state->get_position());

	// A jump while swimming is a push off the surface, which is what gets a
	// character out of a pool instead of bobbing against the lip of it.
	// A jump while swimming is a request to leave the water, and it hands over to
	// falling in the same tick rather than staying to swim upward. Unreal does the
	// same thing for the same reason: it applies OutofWaterZ from
	// PhysicsVolumeChanged, once the character is already MOVE_Falling, because
	// swimming would brake the impulse straight back down - the speed cap and the
	// surface damping both bite a rise this fast, and between them the character
	// never gets out of the pool.
	const bool jumping_out = input->get_want_jump() && component->get_out_of_water_jump_velocity() > 0.0f;
	if (jumping_out)
	{
		velocity += up * component->get_out_of_water_jump_velocity();
	}

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

	// Nothing is being stood on in open water.
	out_state->clear_base();

	if (jumping_out)
	{
		component->queue_next_mode(StringName(CharacterMovementComponent::MODE_FALLING));
	}
}

void SwimmingMode::_bind_methods()
{
}
