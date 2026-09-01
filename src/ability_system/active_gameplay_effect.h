#ifndef ACTIVE_GAMEPLAY_EFFECT_H
#define ACTIVE_GAMEPLAY_EFFECT_H

#include <godot_cpp/classes/ref_counted.hpp>
#include "ability_system/gameplay_effect.h"

using namespace godot;

namespace GFGD
{

// Runtime handle for a non-instant GameplayEffect applied to an AbilitySystemComponent.
class ActiveGameplayEffect : public RefCounted
{
	GDCLASS(ActiveGameplayEffect, RefCounted)

public:
	Ref<GameplayEffect> effect;
	int64_t active_id = 0;
	double remaining_time = 0.0;
	double period_accumulator = 0.0;

	Ref<GameplayEffect> get_effect() const { return effect; }
	int64_t get_active_id() const { return active_id; }
	double get_remaining_time() const { return remaining_time; }

protected:
	static void _bind_methods();
};

}

#endif
