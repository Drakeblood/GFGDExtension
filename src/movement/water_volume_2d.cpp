#include "movement/water_volume_2d.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;
using namespace GFGD;

WaterVolume2D::WaterVolume2D()
	: buoyancy(1.0f)
	, water_velocity(Vector2())
{
	// Nothing should be pushed around by water; it is only ever asked about.
	set_monitoring(false);
	set_monitorable(true);
}

WaterVolume2D::~WaterVolume2D()
{
}

void WaterVolume2D::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_buoyancy"), &WaterVolume2D::get_buoyancy);
	ClassDB::bind_method(D_METHOD("set_buoyancy", "value"), &WaterVolume2D::set_buoyancy);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "buoyancy", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater"), "set_buoyancy", "get_buoyancy");

	ClassDB::bind_method(D_METHOD("get_water_velocity"), &WaterVolume2D::get_water_velocity);
	ClassDB::bind_method(D_METHOD("set_water_velocity", "value"), &WaterVolume2D::set_water_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "water_velocity", PROPERTY_HINT_NONE, "suffix:px/s"), "set_water_velocity", "get_water_velocity");
}
