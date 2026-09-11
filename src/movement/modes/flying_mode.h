#ifndef FLYING_MODE_H
#define FLYING_MODE_H

#include "movement/movement_mode.h"

using namespace godot;

namespace GFGD
{
// Moving freely in three dimensions, with no gravity and no floor.
//
// Unlike walking, the input is not flattened against anything: whatever
// direction the pawn asked for is the direction it accelerates in. A game that
// wants a swimming or hovering feel starts here and changes the numbers rather
// than the code.
class FlyingMode : public MovementMode
{
	GDCLASS(FlyingMode, MovementMode)

public:
	FlyingMode();
	~FlyingMode();

	virtual void generate_move(const Ref<MovementTickParams>& params) override;
	virtual void simulation_tick(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};
}

#endif
