#include "movement/water_volume.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;
using namespace GFGD;

WaterVolume::WaterVolume()
	: buoyancy(1.0f)
	, water_velocity(Vector3())
{
	// Nothing should be pushed around by water; it is only ever asked about.
	set_monitoring(false);
	set_monitorable(true);
}

WaterVolume::~WaterVolume()
{
}

void WaterVolume::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_buoyancy"), &WaterVolume::get_buoyancy);
	ClassDB::bind_method(D_METHOD("set_buoyancy", "value"), &WaterVolume::set_buoyancy);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "buoyancy", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater"), "set_buoyancy", "get_buoyancy");

	ClassDB::bind_method(D_METHOD("get_water_velocity"), &WaterVolume::get_water_velocity);
	ClassDB::bind_method(D_METHOD("set_water_velocity", "value"), &WaterVolume::set_water_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "water_velocity", PROPERTY_HINT_NONE, "suffix:m/s"), "set_water_velocity", "get_water_velocity");
}
