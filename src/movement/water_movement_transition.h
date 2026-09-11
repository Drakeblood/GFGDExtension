#ifndef WATER_MOVEMENT_TRANSITION_H
#define WATER_MOVEMENT_TRANSITION_H

#include "movement/movement_mode_transition.h"

using namespace godot;

namespace GFGD
{
// Into the water and out again.
//
// Registered as a global transition by default, so dropping a WaterVolume into a
// level is the whole of making a character swim. The cost is one point query per
// tick per character; a game with no water anywhere can clear
// CharacterMovementComponent::transitions to stop paying it.
//
// It is a transition rather than something the modes do themselves because
// neither walking nor swimming should have to know the other exists - that is
// the entire argument for transitions being objects.
class WaterMovementTransition : public MovementModeTransition
{
	GDCLASS(WaterMovementTransition, MovementModeTransition)

public:
	WaterMovementTransition();
	~WaterMovementTransition();

	virtual StringName evaluate(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};
}

#endif
