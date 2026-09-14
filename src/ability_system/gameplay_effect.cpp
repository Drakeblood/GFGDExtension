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
	execute_period_on_application = false;
	stacking_type = STACKING_NONE;
	stack_limit = 0;
	stack_duration_refresh = true;
	stack_expiration = CLEAR_ENTIRE_STACK;

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

	BIND_ENUM_CONSTANT(STACKING_NONE);
	BIND_ENUM_CONSTANT(STACKING_AGGREGATE);

	BIND_ENUM_CONSTANT(CLEAR_ENTIRE_STACK);
	BIND_ENUM_CONSTANT(REMOVE_SINGLE_AND_REFRESH);

	ClassDB::bind_method(D_METHOD("get_duration_policy"), &GameplayEffect::get_duration_policy);
	ClassDB::bind_method(D_METHOD("set_duration_policy", "value"), &GameplayEffect::set_duration_policy);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "duration_policy", PROPERTY_HINT_ENUM, "Instant,Infinite,Has Duration"), "set_duration_policy", "get_duration_policy");

	ClassDB::bind_method(D_METHOD("get_duration"), &GameplayEffect::get_duration);
	ClassDB::bind_method(D_METHOD("set_duration", "value"), &GameplayEffect::set_duration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater,suffix:s"), "set_duration", "get_duration");

	ClassDB::bind_method(D_METHOD("get_period"), &GameplayEffect::get_period);
	ClassDB::bind_method(D_METHOD("set_period", "value"), &GameplayEffect::set_period);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "period", PROPERTY_HINT_RANGE, "0,3600,0.01,or_greater,suffix:s"), "set_period", "get_period");

	ClassDB::bind_method(D_METHOD("get_execute_period_on_application"), &GameplayEffect::get_execute_period_on_application);
	ClassDB::bind_method(D_METHOD("set_execute_period_on_application", "value"), &GameplayEffect::set_execute_period_on_application);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "execute_period_on_application"), "set_execute_period_on_application", "get_execute_period_on_application");

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

	ClassDB::bind_method(D_METHOD("get_gameplay_cue_tags"), &GameplayEffect::get_gameplay_cue_tags);
	ClassDB::bind_method(D_METHOD("set_gameplay_cue_tags", "value"), &GameplayEffect::set_gameplay_cue_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "gameplay_cue_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_gameplay_cue_tags", "get_gameplay_cue_tags");

	ADD_GROUP("Stacking", "");

	ClassDB::bind_method(D_METHOD("get_stacking_type"), &GameplayEffect::get_stacking_type);
	ClassDB::bind_method(D_METHOD("set_stacking_type", "value"), &GameplayEffect::set_stacking_type);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "stacking_type", PROPERTY_HINT_ENUM, "None,Aggregate"), "set_stacking_type", "get_stacking_type");

	ClassDB::bind_method(D_METHOD("get_stack_limit"), &GameplayEffect::get_stack_limit);
	ClassDB::bind_method(D_METHOD("set_stack_limit", "value"), &GameplayEffect::set_stack_limit);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "stack_limit", PROPERTY_HINT_RANGE, "0,100,1,or_greater"), "set_stack_limit", "get_stack_limit");

	ClassDB::bind_method(D_METHOD("get_stack_duration_refresh"), &GameplayEffect::get_stack_duration_refresh);
	ClassDB::bind_method(D_METHOD("set_stack_duration_refresh", "value"), &GameplayEffect::set_stack_duration_refresh);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stack_duration_refresh"), "set_stack_duration_refresh", "get_stack_duration_refresh");

	ClassDB::bind_method(D_METHOD("get_stack_expiration"), &GameplayEffect::get_stack_expiration);
	ClassDB::bind_method(D_METHOD("set_stack_expiration", "value"), &GameplayEffect::set_stack_expiration);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "stack_expiration", PROPERTY_HINT_ENUM, "Clear Entire Stack,Remove Single And Refresh"), "set_stack_expiration", "get_stack_expiration");
}
}
