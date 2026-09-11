#ifndef FALLING_MODE_H
#define FALLING_MODE_H

#include "movement/movement_mode.h"

using namespace godot;

namespace GFGD
{
// In the air, under gravity, with whatever steering the game allows.
//
// air_control is a fraction of the ground acceleration rather than a speed of
// its own, so a character that accelerates hard on the ground also turns harder
// in the air - which is usually what a designer means, and is one number to tune
// instead of two that drift apart.
class FallingMode : public MovementMode
{
	GDCLASS(FallingMode, MovementMode)

public:
	FallingMode();
	~FallingMode();

	virtual void generate_move(const Ref<MovementTickParams>& params) override;
	virtual void simulation_tick(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};
}

#endif
