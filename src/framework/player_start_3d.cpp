#include "framework/player_start_3d.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
PlayerStart3D::PlayerStart3D()
{

}

PlayerStart3D::~PlayerStart3D()
{

}

void PlayerStart3D::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_player_start_tag"), &PlayerStart3D::get_player_start_tag);
	ClassDB::bind_method(D_METHOD("set_player_start_tag", "value"), &PlayerStart3D::set_player_start_tag);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "player_start_tag"), "set_player_start_tag", "get_player_start_tag");
}
}
