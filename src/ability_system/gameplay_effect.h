#ifndef GAMEPLAY_EFFECT_H
#define GAMEPLAY_EFFECT_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "gameplay_tags/gameplay_tag_container.h"
#include "ability_system/attribute_modifier.h"

using namespace godot;

namespace GFGD
{

class GameplayEffect : public Resource
{
	GDCLASS(GameplayEffect, Resource)

public:
	enum DurationPolicy
	{
		INSTANT,
		INFINITE,
		HAS_DURATION
	};

private:
	DurationPolicy duration_policy;
	float duration;
	float period;

	TypedArray<AttributeModifier> modifiers;

	Ref<GameplayTagContainer> effect_tags;
	Ref<GameplayTagContainer> granted_tags;
	Ref<GameplayTagContainer> application_required_tags;
	Ref<GameplayTagContainer> application_blocked_tags;
	Ref<GameplayTagContainer> remove_effects_with_tags;

public:
	GameplayEffect();
	~GameplayEffect();

	DurationPolicy get_duration_policy() const { return duration_policy; }
	void set_duration_policy(DurationPolicy value) { duration_policy = value; }

	float get_duration() const { return duration; }
	void set_duration(float value) { duration = value; }

	float get_period() const { return period; }
	void set_period(float value) { period = value; }

	TypedArray<AttributeModifier> get_modifiers() const { return modifiers; }
	void set_modifiers(const TypedArray<AttributeModifier>& value) { modifiers = value; }

	Ref<GameplayTagContainer> get_effect_tags() const { return effect_tags; }
	void set_effect_tags(const Ref<GameplayTagContainer>& value) { effect_tags = value; }

	Ref<GameplayTagContainer> get_granted_tags() const { return granted_tags; }
	void set_granted_tags(const Ref<GameplayTagContainer>& value) { granted_tags = value; }

	Ref<GameplayTagContainer> get_application_required_tags() const { return application_required_tags; }
	void set_application_required_tags(const Ref<GameplayTagContainer>& value) { application_required_tags = value; }

	Ref<GameplayTagContainer> get_application_blocked_tags() const { return application_blocked_tags; }
	void set_application_blocked_tags(const Ref<GameplayTagContainer>& value) { application_blocked_tags = value; }

	Ref<GameplayTagContainer> get_remove_effects_with_tags() const { return remove_effects_with_tags; }
	void set_remove_effects_with_tags(const Ref<GameplayTagContainer>& value) { remove_effects_with_tags = value; }

protected:
	static void _bind_methods();
};

}

VARIANT_ENUM_CAST(GFGD::GameplayEffect::DurationPolicy);

#endif
