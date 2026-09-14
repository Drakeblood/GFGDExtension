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

	// What applying an effect that is already active does.
	enum StackingType
	{
		// Every application is its own active effect.
		STACKING_NONE = 0,

		// Applications of this effect on one target share a single active effect
		// and add to its stack count, up to stack_limit. Unreal's
		// AggregateByTarget.
		STACKING_AGGREGATE = 1,
	};

	// What a stacked effect does when its duration runs out.
	enum StackExpiration
	{
		// The whole stack goes at once.
		CLEAR_ENTIRE_STACK = 0,

		// One stack goes, and the duration starts again for the rest - a poison
		// that wears off one dose at a time.
		REMOVE_SINGLE_AND_REFRESH = 1,
	};

private:
	DurationPolicy duration_policy;
	float duration;
	float period;
	bool execute_period_on_application;

	StackingType stacking_type;
	int stack_limit;
	bool stack_duration_refresh;
	StackExpiration stack_expiration;

	TypedArray<AttributeModifier> modifiers;

	Ref<GameplayTagContainer> effect_tags;
	Ref<GameplayTagContainer> granted_tags;
	Ref<GameplayTagContainer> application_required_tags;
	Ref<GameplayTagContainer> application_blocked_tags;
	Ref<GameplayTagContainer> remove_effects_with_tags;

	// Cues for the presentation layer - a sound, a flash, a status icon. Fired
	// on every peer: executed for an instant effect and for each period tick,
	// added and removed with a duration or infinite one.
	Ref<GameplayTagContainer> gameplay_cue_tags;

public:
	GameplayEffect();
	~GameplayEffect();

	DurationPolicy get_duration_policy() const { return duration_policy; }
	void set_duration_policy(DurationPolicy value) { duration_policy = value; }

	float get_duration() const { return duration; }
	void set_duration(float value) { duration = value; }

	float get_period() const { return period; }
	void set_period(float value) { period = value; }

	bool get_execute_period_on_application() const { return execute_period_on_application; }
	void set_execute_period_on_application(bool value) { execute_period_on_application = value; }

	StackingType get_stacking_type() const { return stacking_type; }
	void set_stacking_type(StackingType value) { stacking_type = value; }

	int get_stack_limit() const { return stack_limit; }
	void set_stack_limit(int value) { stack_limit = MAX(0, value); }

	bool get_stack_duration_refresh() const { return stack_duration_refresh; }
	void set_stack_duration_refresh(bool value) { stack_duration_refresh = value; }

	StackExpiration get_stack_expiration() const { return stack_expiration; }
	void set_stack_expiration(StackExpiration value) { stack_expiration = value; }

	Ref<GameplayTagContainer> get_gameplay_cue_tags() const { return gameplay_cue_tags; }
	void set_gameplay_cue_tags(const Ref<GameplayTagContainer>& value) { gameplay_cue_tags = value; }

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
VARIANT_ENUM_CAST(GFGD::GameplayEffect::StackingType);
VARIANT_ENUM_CAST(GFGD::GameplayEffect::StackExpiration);

#endif
