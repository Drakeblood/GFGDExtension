#include "ability_system/gameplay_effect.h"
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tags/gameplay_tag_container.h"
#include "ability_system/attribute_modifier.h"

using namespace godot;

namespace GFGD
{
GameplayEffect::GameplayEffect()
{
	duration_policy = INSTANT;
	duration = 0.0f;
	period = 0.0f;

	// Left null on purpose - see GameplayAbility's constructor.
}

GameplayEffect::~GameplayEffect()
{

}

void GameplayEffect::_bind_methods()
{
	BIND_ENUM_CONSTANT(INSTANT);
	BIND_ENUM_CONSTANT(INFINITE);
	BIND_ENUM_CONSTANT(HAS_DURATION);

	ClassDB::bind_method(D_METHOD("get_duration_policy"), &GameplayEffect::get_duration_policy);
	ClassDB::bind_method(D_METHOD("set_duration_policy", "value"), &GameplayEffect::set_duration_policy);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "duration_policy", PROPERTY_HINT_ENUM, "Instant,Infinite,Has Duration"), "set_duration_policy", "get_duration_policy");

	ClassDB::bind_method(D_METHOD("get_duration"), &GameplayEffect::get_duration);
	ClassDB::bind_method(D_METHOD("set_duration", "value"), &GameplayEffect::set_duration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater,suffix:s"), "set_duration", "get_duration");

	ClassDB::bind_method(D_METHOD("get_period"), &GameplayEffect::get_period);
	ClassDB::bind_method(D_METHOD("set_period", "value"), &GameplayEffect::set_period);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "period", PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater,suffix:s"), "set_period", "get_period");

	ClassDB::bind_method(D_METHOD("get_modifiers"), &GameplayEffect::get_modifiers);
	ClassDB::bind_method(D_METHOD("set_modifiers", "value"), &GameplayEffect::set_modifiers);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "modifiers", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:AttributeModifier", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_modifiers", "get_modifiers");

	ClassDB::bind_method(D_METHOD("get_effect_tags"), &GameplayEffect::get_effect_tags);
	ClassDB::bind_method(D_METHOD("set_effect_tags", "value"), &GameplayEffect::set_effect_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "effect_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_effect_tags", "get_effect_tags");

	ClassDB::bind_method(D_METHOD("get_granted_tags"), &GameplayEffect::get_granted_tags);
	ClassDB::bind_method(D_METHOD("set_granted_tags", "value"), &GameplayEffect::set_granted_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "granted_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_granted_tags", "get_granted_tags");

	ClassDB::bind_method(D_METHOD("get_application_required_tags"), &GameplayEffect::get_application_required_tags);
	ClassDB::bind_method(D_METHOD("set_application_required_tags", "value"), &GameplayEffect::set_application_required_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "application_required_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_application_required_tags", "get_application_required_tags");

	ClassDB::bind_method(D_METHOD("get_application_blocked_tags"), &GameplayEffect::get_application_blocked_tags);
	ClassDB::bind_method(D_METHOD("set_application_blocked_tags", "value"), &GameplayEffect::set_application_blocked_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "application_blocked_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_application_blocked_tags", "get_application_blocked_tags");

	ClassDB::bind_method(D_METHOD("get_remove_effects_with_tags"), &GameplayEffect::get_remove_effects_with_tags);
	ClassDB::bind_method(D_METHOD("set_remove_effects_with_tags", "value"), &GameplayEffect::set_remove_effects_with_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "remove_effects_with_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_remove_effects_with_tags", "get_remove_effects_with_tags");
}
}
