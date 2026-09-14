#ifndef ATTRIBUTE_MODIFIER_H
#define ATTRIBUTE_MODIFIER_H

#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>

using namespace godot;

namespace GFGD
{
class AbilitySystemComponent;
class GameplayEffectSpec;

class AttributeModifier : public Resource
{
	GDCLASS(AttributeModifier, Resource)

public:
	enum Operation
	{
		ADD = 0,
		MULTIPLY = 1,
		OVERRIDE = 2,
	};

	// Where the number comes from. Unreal's FGameplayEffectModifierMagnitude,
	// minus custom calculation classes: anything stranger than these three is a
	// SET_BY_CALLER value the code making the spec works out itself.
	enum MagnitudeType
	{
		// magnitude, times level_curve sampled at the spec's level when a curve
		// is set.
		SCALABLE_FLOAT = 0,

		// (backing attribute + pre_multiply_additive) * coefficient
		// + post_multiply_additive, the attribute read from the source or the
		// target when the effect is applied.
		ATTRIBUTE_BASED = 1,

		// Whatever the spec was given under set_by_caller_name; magnitude when it
		// was given nothing.
		SET_BY_CALLER = 2,
	};

	enum AttributeSource
	{
		SOURCE = 0,
		TARGET = 1,
	};

private:
	StringName attribute;
	Operation operation;
	double magnitude;

	MagnitudeType magnitude_type;
	Ref<Curve> level_curve;

	StringName backing_attribute;
	AttributeSource attribute_source;
	double coefficient;
	double pre_multiply_additive;
	double post_multiply_additive;

	StringName set_by_caller_name;

public:
	AttributeModifier();

	StringName get_attribute() const { return attribute; }
	void set_attribute(const StringName& value) { attribute = value; }

	Operation get_operation() const { return operation; }
	void set_operation(Operation value) { operation = value; }

	double get_magnitude() const { return magnitude; }
	void set_magnitude(double value) { magnitude = value; }

	MagnitudeType get_magnitude_type() const { return magnitude_type; }
	void set_magnitude_type(MagnitudeType value) { magnitude_type = value; }

	Ref<Curve> get_level_curve() const { return level_curve; }
	void set_level_curve(const Ref<Curve>& value) { level_curve = value; }

	StringName get_backing_attribute() const { return backing_attribute; }
	void set_backing_attribute(const StringName& value) { backing_attribute = value; }

	AttributeSource get_attribute_source() const { return attribute_source; }
	void set_attribute_source(AttributeSource value) { attribute_source = value; }

	double get_coefficient() const { return coefficient; }
	void set_coefficient(double value) { coefficient = value; }

	double get_pre_multiply_additive() const { return pre_multiply_additive; }
	void set_pre_multiply_additive(double value) { pre_multiply_additive = value; }

	double get_post_multiply_additive() const { return post_multiply_additive; }
	void set_post_multiply_additive(double value) { post_multiply_additive = value; }

	StringName get_set_by_caller_name() const { return set_by_caller_name; }
	void set_set_by_caller_name(const StringName& value) { set_by_caller_name = value; }

	// The magnitude this modifier has for one application. Evaluated once, when
	// the effect is applied - an attribute-based bonus is a snapshot of the
	// attribute at that moment, not a live link to it.
	double calculate_magnitude(const Ref<GameplayEffectSpec>& spec, AbilitySystemComponent* target) const;

	// The operation, with this modifier's authored magnitude.
	double apply(double input_value) const;

	// The operation, with a magnitude calculated elsewhere.
	double apply_magnitude(double input_value, double with_magnitude) const;

protected:
	static void _bind_methods();
};

}

VARIANT_ENUM_CAST(GFGD::AttributeModifier::Operation);
VARIANT_ENUM_CAST(GFGD::AttributeModifier::MagnitudeType);
VARIANT_ENUM_CAST(GFGD::AttributeModifier::AttributeSource);

#endif
