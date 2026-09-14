#include "ability_system/gameplay_effect_spec.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

#include "ability_system/ability_system_component.h"

using namespace godot;

namespace GFGD
{
GameplayEffectSpec::GameplayEffectSpec()
{
	level = 1.0f;
}

GameplayEffectSpec::~GameplayEffectSpec()
{
}

AbilitySystemComponent* GameplayEffectSpec::get_source() const
{
	if (source_id.is_null()) { return nullptr; }
	return Object::cast_to<AbilitySystemComponent>(ObjectDB::get_instance(source_id));
}

void GameplayEffectSpec::set_source(AbilitySystemComponent* value)
{
	source_id = value != nullptr ? value->get_instance_id() : ObjectID();
}

void GameplayEffectSpec::set_set_by_caller_magnitude(const StringName& name, double magnitude)
{
	set_by_caller_magnitudes[name] = magnitude;
}

double GameplayEffectSpec::get_set_by_caller_magnitude(const StringName& name, double default_value) const
{
	return set_by_caller_magnitudes.get(name, default_value);
}

bool GameplayEffectSpec::has_set_by_caller_magnitude(const StringName& name) const
{
	return set_by_caller_magnitudes.has(name);
}

Ref<GameplayEffectSpec> GameplayEffectSpec::duplicate_spec() const
{
	Ref<GameplayEffectSpec> copy;
	copy.instantiate();
	copy->effect = effect;
	copy->level = level;
	copy->source_id = source_id;
	copy->set_by_caller_magnitudes = set_by_caller_magnitudes.duplicate();
	copy->context = context.duplicate(true);
	return copy;
}

void GameplayEffectSpec::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_effect"), &GameplayEffectSpec::get_effect);
	ClassDB::bind_method(D_METHOD("set_effect", "value"), &GameplayEffectSpec::set_effect);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "GameplayEffect"), "set_effect", "get_effect");

	ClassDB::bind_method(D_METHOD("get_level"), &GameplayEffectSpec::get_level);
	ClassDB::bind_method(D_METHOD("set_level", "value"), &GameplayEffectSpec::set_level);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "level"), "set_level", "get_level");

	ClassDB::bind_method(D_METHOD("get_source"), &GameplayEffectSpec::get_source);
	ClassDB::bind_method(D_METHOD("set_source", "value"), &GameplayEffectSpec::set_source);

	ClassDB::bind_method(D_METHOD("set_set_by_caller_magnitude", "name", "magnitude"), &GameplayEffectSpec::set_set_by_caller_magnitude);
	ClassDB::bind_method(D_METHOD("get_set_by_caller_magnitude", "name", "default_value"), &GameplayEffectSpec::get_set_by_caller_magnitude, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("has_set_by_caller_magnitude", "name"), &GameplayEffectSpec::has_set_by_caller_magnitude);
	ClassDB::bind_method(D_METHOD("get_set_by_caller_magnitudes"), &GameplayEffectSpec::get_set_by_caller_magnitudes);

	ClassDB::bind_method(D_METHOD("get_context"), &GameplayEffectSpec::get_context);
	ClassDB::bind_method(D_METHOD("set_context", "value"), &GameplayEffectSpec::set_context);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "context"), "set_context", "get_context");

	ClassDB::bind_method(D_METHOD("duplicate_spec"), &GameplayEffectSpec::duplicate_spec);
}
}
