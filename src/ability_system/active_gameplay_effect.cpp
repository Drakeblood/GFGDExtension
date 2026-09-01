#include "ability_system/active_gameplay_effect.h"
#include <godot_cpp/core/class_db.hpp>

#include "ability_system/gameplay_effect.h"

using namespace godot;

namespace GFGD
{
void ActiveGameplayEffect::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_effect"), &ActiveGameplayEffect::get_effect);
	ClassDB::bind_method(D_METHOD("get_active_id"), &ActiveGameplayEffect::get_active_id);
	ClassDB::bind_method(D_METHOD("get_remaining_time"), &ActiveGameplayEffect::get_remaining_time);
}
}
