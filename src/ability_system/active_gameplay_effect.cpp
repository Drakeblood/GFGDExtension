#include "ability_system/active_gameplay_effect.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "ability_system/attribute_modifier.h"
#include "ability_system/gameplay_effect.h"

using namespace godot;

namespace GFGD
{
double ActiveGameplayEffect::get_stacked_magnitude(int modifier_index, int operation) const
{
	if (modifier_index < 0 || modifier_index >= magnitudes.size()) { return 0.0; }

	const double magnitude = magnitudes[modifier_index];
	const int stacks = MAX(1, stack_count);

	switch (operation)
	{
		case AttributeModifier::MULTIPLY:
			return Math::pow(magnitude, (double)stacks);
		case AttributeModifier::OVERRIDE:
			return magnitude;
		case AttributeModifier::ADD:
		default:
			return magnitude * stacks;
	}
}

void ActiveGameplayEffect::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_effect"), &ActiveGameplayEffect::get_effect);
	ClassDB::bind_method(D_METHOD("get_spec"), &ActiveGameplayEffect::get_spec);
	ClassDB::bind_method(D_METHOD("get_active_id"), &ActiveGameplayEffect::get_active_id);
	ClassDB::bind_method(D_METHOD("get_duration"), &ActiveGameplayEffect::get_duration);
	ClassDB::bind_method(D_METHOD("get_remaining_time"), &ActiveGameplayEffect::get_remaining_time);
	ClassDB::bind_method(D_METHOD("get_stack_count"), &ActiveGameplayEffect::get_stack_count);
	ClassDB::bind_method(D_METHOD("get_level"), &ActiveGameplayEffect::get_level);
	ClassDB::bind_method(D_METHOD("is_replicated"), &ActiveGameplayEffect::is_replicated);
	ClassDB::bind_method(D_METHOD("get_stacked_magnitude", "modifier_index", "operation"), &ActiveGameplayEffect::get_stacked_magnitude);
}
}
