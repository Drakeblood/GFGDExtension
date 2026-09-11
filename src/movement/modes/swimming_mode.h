#ifndef SWIMMING_MODE_H
#define SWIMMING_MODE_H

#include "movement/movement_mode.h"

using namespace godot;

namespace GFGD
{
// Moving through water.
//
// Like flying, the input is not flattened - you swim in whatever direction you
// point. Unlike flying, gravity is not gone: it is scaled by how much of the
// character is under the surface and by the volume's buoyancy, so a character at
// the surface sinks until it is submerged and then hangs there. Floating up to
// the surface is a buoyancy above 1.
class SwimmingMode : public MovementMode
{
	GDCLASS(SwimmingMode, MovementMode)

public:
	SwimmingMode();
	~SwimmingMode();

	virtual void generate_move(const Ref<MovementTickParams>& params) override;
	virtual void simulation_tick(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};
}

#endif
