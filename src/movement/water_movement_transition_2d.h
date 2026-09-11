#ifndef WATER_MOVEMENT_TRANSITION_2D_H
#define WATER_MOVEMENT_TRANSITION_2D_H

#include "movement/movement_mode_transition.h"

using namespace godot;

namespace GFGD
{
// Into the water and out again, in two dimensions.
//
// Registered as a global transition by default, so dropping a WaterVolume2D into
// a level is the whole of making a character swim. The cost is one point query
// per tick per character; a game with no water anywhere can clear
// CharacterMovementComponent2D::transitions to stop paying it.
class WaterMovementTransition2D : public MovementModeTransition
{
	GDCLASS(WaterMovementTransition2D, MovementModeTransition)

public:
	WaterMovementTransition2D();
	~WaterMovementTransition2D();

	virtual StringName evaluate(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};
}

#endif
