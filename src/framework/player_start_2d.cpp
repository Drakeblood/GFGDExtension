#include "framework/player_start_2d.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
PlayerStart2D::PlayerStart2D()
{

}

PlayerStart2D::~PlayerStart2D()
{

}

void PlayerStart2D::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_player_start_tag"), &PlayerStart2D::get_player_start_tag);
	ClassDB::bind_method(D_METHOD("set_player_start_tag", "value"), &PlayerStart2D::set_player_start_tag);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "player_start_tag"), "set_player_start_tag", "get_player_start_tag");
}
}
