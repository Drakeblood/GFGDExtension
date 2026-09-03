#include "framework/level.h"
#include <godot_cpp/core/class_db.hpp>

#include "framework/world.h"

using namespace godot;

namespace GFGD
{
Level::Level()
{

}

Level::~Level()
{

}

void Level::init_level(World* world)
{
	GDVIRTUAL_CALL(_init_level, world);
}

void Level::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_game_mode_override"), &Level::get_game_mode_override);
	ClassDB::bind_method(D_METHOD("set_game_mode_override", "value"), &Level::set_game_mode_override);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_mode_override", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_game_mode_override", "get_game_mode_override");

	GDVIRTUAL_BIND(_init_level, "world");
}
}
