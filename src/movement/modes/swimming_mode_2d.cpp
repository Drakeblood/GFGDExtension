#include "movement/modes/swimming_mode_2d.h"

#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component_2d.h"
#include "movement/movement_utils_2d.h"
#include "movement/water_volume_2d.h"

using namespace godot;
using namespace GFGD;

SwimmingMode2D::SwimmingMode2D()
{
}

SwimmingMode2D::~SwimmingMode2D()
{
}

void SwimmingMode2D::generate_move(const Ref<MovementTickParams>& params)
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
	const Vector2 position = MovementUtils2D::to_2d(state->get_position());
	const Transform2D transform(0.0, position);

	const float immersion = component->get_immersion_depth_at(transform);
	const WaterVolume2D* water = Object::cast_to<WaterVolume2D>(component->find_water_volume_at(position));

	const float buoyancy = water != nullptr ? water->get_buoyancy() : 1.0f;
	const Vector2 current = water != nullptr ? water->get_water_velocity() : Vector2();

	const Vector2 move_input = MovementUtils2D::to_2d(input->get_move_input());
	Vector2 acceleration = move_input * component->get_max_acceleration();

	// Nothing to push against above the surface. The part of the stroke that
	// lifts is scaled by how much of the character the water still holds, so a
	// character holding "up" treads water at the waterline instead of swimming
	// clean out of the pool, arcing through the air and dropping back in. It did
	// exactly that before: the mode stays swimming until the feet clear, and a
	// stroke of 2048 px/s² out-accelerates gravity two to one, so the capsule
	// left the water under power. Getting out is the jump's job.
	const float lift = acceleration.dot(up);
	if (lift > 0.0f)
	{
		acceleration -= up * (lift * (1.0f - immersion));
	}

	Vector2 velocity = MovementUtils2D::compute_velocity(
		MovementUtils2D::to_2d(state->get_velocity()) - current,
		acceleration,
		component->get_ground_friction(),
		component->get_braking_deceleration_swimming(),
		component->get_braking_friction_factor(),
		component->get_analog_max_speed(move_input, component->get_max_swim_speed()),
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

	proposed->set_linear_velocity(MovementUtils2D::to_3d(velocity + current));
	proposed->set_direction_intent(input->get_move_input());
	proposed->set_has_dir_intent(!move_input.is_zero_approx());
	proposed->set_mix_mode(ProposedMove::OVERRIDE_VELOCITY);
}

void SwimmingMode2D::simulation_tick(const Ref<MovementTickParams>& params)
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
	Vector2 velocity = MovementUtils2D::to_2d(proposed->get_linear_velocity());
	const Transform2D transform(0.0, MovementUtils2D::to_2d(start_state->get_position()));

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

	const MovementUtils2D::SlideResult slide = MovementUtils2D::slide_move(
		body,
		transform,
		velocity * (float)params->get_delta(),
		velocity,
		up,
		component->get_walkable_floor_cos(),
		component->get_max_slides());

	out_state->set_position(MovementUtils2D::to_3d(slide.transform.get_origin()));
	out_state->set_velocity(MovementUtils2D::to_3d(slide.velocity));

	// Nothing is being stood on in open water.
	out_state->clear_base();

	if (jumping_out)
	{
		component->queue_next_mode(StringName(CharacterMovementComponent2D::MODE_FALLING));
	}
}

void SwimmingMode2D::_bind_methods()
{
}
