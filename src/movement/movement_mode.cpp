#include "movement/movement_mode.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;
using namespace GFGD;

// --- MovementMode -----------------------------------------------------------

MovementMode::MovementMode()
{
}

MovementMode::~MovementMode()
{
}

void MovementMode::generate_move(const Ref<MovementTickParams>& params)
{
	GDVIRTUAL_CALL(_generate_move, params);
}

void MovementMode::simulation_tick(const Ref<MovementTickParams>& params)
{
	GDVIRTUAL_CALL(_simulation_tick, params);
}

void MovementMode::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_transitions"), &MovementMode::get_transitions);
	ClassDB::bind_method(D_METHOD("set_transitions", "value"), &MovementMode::set_transitions);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transitions", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:MovementModeTransition", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_transitions", "get_transitions");

	GDVIRTUAL_BIND(_generate_move, "params");
	GDVIRTUAL_BIND(_simulation_tick, "params");
}

// --- NullMovementMode -------------------------------------------------------

NullMovementMode::NullMovementMode()
{
}

NullMovementMode::~NullMovementMode()
{
}

void NullMovementMode::generate_move(const Ref<MovementTickParams>& params)
{
	if (params.is_valid() && params->get_proposed_move().is_valid())
	{
		params->get_proposed_move()->reset();
	}
}

void NullMovementMode::simulation_tick(const Ref<MovementTickParams>& params)
{
	// out_state already holds a copy of the starting state, which is exactly
	// "nothing happened".
}

void NullMovementMode::_bind_methods()
{
}
