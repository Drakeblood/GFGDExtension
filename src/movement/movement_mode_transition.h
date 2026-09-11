#ifndef MOVEMENT_MODE_TRANSITION_H
#define MOVEMENT_MODE_TRANSITION_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "movement/movement_types.h"

using namespace godot;

namespace GFGD
{
// A rule for leaving one movement mode for another, as an object.
//
// The point is where the rule lives. A mode that decides its own exits has every
// such decision written into it, so adding "climb a ladder when one is in reach"
// means editing the walking mode - and then editing it again for the next idea.
// A transition is authored beside the mode instead, or globally on the component,
// and the mode it leaves never hears about it.
//
// Evaluated after the mode has run, in order: the active mode's own list first,
// then the component's global one. The first that answers with a name wins, so
// order is priority.
class MovementModeTransition : public Resource
{
	GDCLASS(MovementModeTransition, Resource)

public:
	MovementModeTransition();
	~MovementModeTransition();

	// The mode to switch to, or an empty name for "not now". Called every tick
	// while the mode it belongs to is active, so it should be cheap and it must
	// not move anything.
	virtual StringName evaluate(const Ref<MovementTickParams>& params);

	GDVIRTUAL1R(StringName, _evaluate, Ref<MovementTickParams>)

protected:
	static void _bind_methods();
};
}

#endif
