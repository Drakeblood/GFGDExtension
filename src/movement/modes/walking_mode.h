#ifndef WALKING_MODE_H
#define WALKING_MODE_H

#include "movement/movement_mode.h"

using namespace godot;

namespace GFGD
{
// Moving while there is walkable ground underneath.
//
// Velocity is kept flat against the floor plane rather than in world horizontal,
// which is what makes a slope feel like a slope instead of a series of small
// falls. Losing the floor - walking off a ledge, or onto something too steep -
// hands over to the falling mode; jumping does the same thing deliberately.
class WalkingMode : public MovementMode
{
	GDCLASS(WalkingMode, MovementMode)

public:
	WalkingMode();
	~WalkingMode();

	virtual void generate_move(const Ref<MovementTickParams>& params) override;
	virtual void simulation_tick(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};
}

#endif
