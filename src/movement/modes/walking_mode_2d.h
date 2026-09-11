#ifndef WALKING_MODE_2D_H
#define WALKING_MODE_2D_H

#include "movement/movement_mode.h"

using namespace godot;

namespace GFGD
{
// Walking, in two dimensions. The 3D mode's twin.
class WalkingMode2D : public MovementMode
{
	GDCLASS(WalkingMode2D, MovementMode)

public:
	WalkingMode2D();
	~WalkingMode2D();

	virtual void generate_move(const Ref<MovementTickParams>& params) override;
	virtual void simulation_tick(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};

// Falling, in two dimensions.
class FallingMode2D : public MovementMode
{
	GDCLASS(FallingMode2D, MovementMode)

public:
	FallingMode2D();
	~FallingMode2D();

	virtual void generate_move(const Ref<MovementTickParams>& params) override;
	virtual void simulation_tick(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};

// Flying, in two dimensions: no gravity, no floor, and the input is not
// flattened. Nothing enters it on its own - that is a transition's job.
class FlyingMode2D : public MovementMode
{
	GDCLASS(FlyingMode2D, MovementMode)

public:
	FlyingMode2D();
	~FlyingMode2D();

	virtual void generate_move(const Ref<MovementTickParams>& params) override;
	virtual void simulation_tick(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};
}

#endif
