#include "movement/water_movement_transition.h"

#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component.h"

using namespace godot;
using namespace GFGD;

WaterMovementTransition::WaterMovementTransition()
{
}

WaterMovementTransition::~WaterMovementTransition()
{
}

StringName WaterMovementTransition::evaluate(const Ref<MovementTickParams>& params)
{
	// A script subclass gets the first word, the same way every other virtual
	// here works.
	const StringName scripted = MovementModeTransition::evaluate(params);
	if (scripted != StringName())
	{
		return scripted;
	}

	if (params.is_null())
	{
		return StringName();
	}

	CharacterMovementComponent* component = params->get_component();
	const Ref<MovementState> state = params->get_out_state();
	if (component == nullptr || state.is_null())
	{
		return StringName();
	}

	const Transform3D transform(state->get_rotation(), state->get_position());
	const bool swimming = state->get_movement_mode() == StringName(CharacterMovementComponent::MODE_SWIMMING);

	// Deep enough to swim is measured at the character's middle, not its feet:
	// wading through a shallow river is walking, and only goes under when the
	// water is up to the chest.
	const bool submerged = component->get_immersion_depth_at(transform) > 0.0f;

	if (!swimming)
	{
		if (!submerged)
		{
			return StringName();
		}

		// Not while shooting upward out of the water. The jump out is applied in
		// falling, where nothing caps it, and grabbing the character back here
		// would put it straight under the swim speed cap that the jump exists to
		// escape. Half the jump velocity is well clear of anything gravity or a
		// wave produces, and a character walking or diving in is never rising.
		const float rising = state->get_velocity().dot(component->get_up_direction());
		if (rising > 0.5f * component->get_out_of_water_jump_velocity())
		{
			return StringName();
		}

		return StringName(CharacterMovementComponent::MODE_SWIMMING);
	}

	// Entering is decided at the middle, leaving at the feet, and the asymmetry is
	// the point. A character treading water in the middle of a lake has its head
	// out and its middle on the waterline - testing the same place in both
	// directions makes it cross twice a second, swimming, falling, swimming,
	// while it bobs. Requiring the feet to clear means it stays swimming until
	// the whole capsule is out, which is the only moment it has actually gone
	// somewhere.
	//
	// Shallow water never traps anyone here: entering needs the middle submerged,
	// so wading was never swimming in the first place.
	const Vector3 exit_sample = transform.origin - component->get_up_direction() * (component->get_capsule_half_height() * 0.9f);
	if (component->find_water_volume_at(exit_sample) != nullptr)
	{
		return StringName();
	}

	// Out of the water and into the air. Falling will hand over to walking on its
	// own if there is ground underneath.
	return StringName(CharacterMovementComponent::MODE_FALLING);
}

void WaterMovementTransition::_bind_methods()
{
}
