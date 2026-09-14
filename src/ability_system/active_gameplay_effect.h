#ifndef ACTIVE_GAMEPLAY_EFFECT_H
#define ACTIVE_GAMEPLAY_EFFECT_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>

#include "ability_system/gameplay_effect.h"
#include "ability_system/gameplay_effect_spec.h"

using namespace godot;

namespace GFGD
{

// Runtime handle for a non-instant GameplayEffect applied to an AbilitySystemComponent.
//
// On a client under replication it is a mirror of the server's: the effect
// (loaded from its resource path, or rebuilt from what was sent when it has
// none), the stack count and the time left, counting down locally between
// updates. The mirror carries no spec and changes no attribute - the server's
// attribute values arrive on their own.
class ActiveGameplayEffect : public RefCounted
{
	GDCLASS(ActiveGameplayEffect, RefCounted)

public:
	Ref<GameplayEffect> effect;
	Ref<GameplayEffectSpec> spec;
	int64_t active_id = 0;
	double duration = 0.0;
	double remaining_time = 0.0;
	double period_accumulator = 0.0;
	int stack_count = 1;

	// One per modifier of the effect, calculated when it was applied.
	PackedFloat64Array magnitudes;

	bool replicated = false;

	Ref<GameplayEffect> get_effect() const { return effect; }
	Ref<GameplayEffectSpec> get_spec() const { return spec; }
	int64_t get_active_id() const { return active_id; }
	double get_duration() const { return duration; }
	double get_remaining_time() const { return remaining_time; }
	int get_stack_count() const { return stack_count; }
	float get_level() const { return spec.is_valid() ? spec->get_level() : 1.0f; }
	bool is_replicated() const { return replicated; }

	// The modifier's magnitude as it applies right now: calculated at
	// application, scaled by the stack - added once per stack for ADD, raised to
	// the stack for MULTIPLY, and left alone for OVERRIDE.
	double get_stacked_magnitude(int modifier_index, int operation) const;

protected:
	static void _bind_methods();
};

}

#endif
