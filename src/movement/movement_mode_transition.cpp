#include "movement/movement_mode_transition.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;
using namespace GFGD;

MovementModeTransition::MovementModeTransition()
{
}

MovementModeTransition::~MovementModeTransition()
{
}

StringName MovementModeTransition::evaluate(const Ref<MovementTickParams>& params)
{
	StringName result;
	GDVIRTUAL_CALL(_evaluate, params, result);
	return result;
}

void MovementModeTransition::_bind_methods()
{
	GDVIRTUAL_BIND(_evaluate, "params");
}
