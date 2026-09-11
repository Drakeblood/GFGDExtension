#ifndef WATER_VOLUME_H
#define WATER_VOLUME_H

#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

namespace GFGD
{
// A body of water: put one anywhere a character should swim.
//
// An Area3D, so its shape is authored the way any trigger volume is. The
// movement component does not connect to its signals - it asks the physics
// server which volume is at a point, every tick, from the transform it is
// simulating. Signals would be state accumulated outside the simulation, and a
// replayed tick would see whatever the last enter or exit happened to leave
// behind rather than what was true at that tick.
class WaterVolume : public Area3D
{
	GDCLASS(WaterVolume, Area3D)

private:
	// How strongly the water cancels gravity. 1.0 is neutral: a submerged
	// character neither sinks nor rises. Below 1 it sinks, above 1 it floats up
	// to the surface.
	float buoyancy;

	// A current, in metres per second. Added to whatever the character is doing,
	// so it drifts even while still.
	Vector3 water_velocity;

public:
	WaterVolume();
	~WaterVolume();

	float get_buoyancy() const { return buoyancy; }
	void set_buoyancy(float value) { buoyancy = value; }

	Vector3 get_water_velocity() const { return water_velocity; }
	void set_water_velocity(const Vector3& value) { water_velocity = value; }

protected:
	static void _bind_methods();
};
}

#endif
