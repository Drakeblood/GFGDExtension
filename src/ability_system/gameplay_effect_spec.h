#ifndef GAMEPLAY_EFFECT_SPEC_H
#define GAMEPLAY_EFFECT_SPEC_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "ability_system/gameplay_effect.h"

using namespace godot;

namespace GFGD
{
class AbilitySystemComponent;

// One application of a GameplayEffect: the effect, plus everything about this
// particular use of it that the effect resource cannot know - how strong
// (level), who caused it (source), and values only known at the moment
// (set-by-caller magnitudes, a hit position in context).
//
// The effect stays a shared, authored Resource; a spec is made per use and is
// cheap. Unreal separates the two the same way, as UGameplayEffect and
// FGameplayEffectSpec, and for the same reason: "fireball does 40 at level 3"
// is one asset, not ten.
class GameplayEffectSpec : public RefCounted
{
	GDCLASS(GameplayEffectSpec, RefCounted)

private:
	Ref<GameplayEffect> effect;
	float level;

	// Held by id rather than by pointer: the source may be freed while an effect
	// it applied is still running, and a dangling pointer there would be read on
	// every attribute-based recompute.
	ObjectID source_id;

	Dictionary set_by_caller_magnitudes;
	Dictionary context;

public:
	GameplayEffectSpec();
	~GameplayEffectSpec();

	Ref<GameplayEffect> get_effect() const { return effect; }
	void set_effect(const Ref<GameplayEffect>& value) { effect = value; }

	float get_level() const { return level; }
	void set_level(float value) { level = value; }

	AbilitySystemComponent* get_source() const;
	void set_source(AbilitySystemComponent* value);

	// Values an effect's SET_BY_CALLER modifiers read, by name. Set by whatever
	// makes the spec - "Damage" = 37 for this hit.
	void set_set_by_caller_magnitude(const StringName& name, double magnitude);
	double get_set_by_caller_magnitude(const StringName& name, double default_value = 0.0) const;
	bool has_set_by_caller_magnitude(const StringName& name) const;
	Dictionary get_set_by_caller_magnitudes() const { return set_by_caller_magnitudes; }

	// Free-form data about this application: where it hit, which weapon. Nothing
	// in the framework reads it; it is carried to gameplay cues as parameters.
	Dictionary get_context() const { return context; }
	void set_context(const Dictionary& value) { context = value; }

	Ref<GameplayEffectSpec> duplicate_spec() const;

protected:
	static void _bind_methods();
};

}

#endif
