#include "ability_system/attribute_modifier.h"
#include <godot_cpp/core/class_db.hpp>

#include "ability_system/ability_system_component.h"
#include "ability_system/gameplay_effect_spec.h"

using namespace godot;

namespace GFGD
{
AttributeModifier::AttributeModifier()
{
	operation = ADD;
	magnitude = 0.0;
	magnitude_type = SCALABLE_FLOAT;
	attribute_source = SOURCE;
	coefficient = 1.0;
	pre_multiply_additive = 0.0;
	post_multiply_additive = 0.0;
}

double AttributeModifier::calculate_magnitude(const Ref<GameplayEffectSpec>& spec, AbilitySystemComponent* target) const
{
	switch (magnitude_type)
	{
		case SET_BY_CALLER:
		{
			if (spec.is_null()) { return magnitude; }
			if (!spec->has_set_by_caller_magnitude(set_by_caller_name))
			{
				WARN_PRINT(vformat("GFGD: a SET_BY_CALLER modifier on '%s' found no value named '%s' on its spec; using its magnitude (%f).", String(attribute), String(set_by_caller_name), magnitude));
				return magnitude;
			}
			return spec->get_set_by_caller_magnitude(set_by_caller_name);
		}

		case ATTRIBUTE_BASED:
		{
			// Source is whoever made the spec; without one - an effect applied
			// to self with no spec - the target stands in for it.
			AbilitySystemComponent* from = target;
			if (attribute_source == SOURCE && spec.is_valid() && spec->get_source() != nullptr)
			{
				from = spec->get_source();
			}

			const double backing = from != nullptr ? from->get_attribute_value(backing_attribute) : 0.0;
			return (backing + pre_multiply_additive) * coefficient + post_multiply_additive;
		}

		case SCALABLE_FLOAT:
		default:
		{
			if (level_curve.is_null() || spec.is_null()) { return magnitude; }
			return magnitude * level_curve->sample(spec->get_level());
		}
	}
}

double AttributeModifier::apply(double input_value) const
{
	return apply_magnitude(input_value, magnitude);
}

double AttributeModifier::apply_magnitude(double input_value, double with_magnitude) const
{
	switch (operation)
	{
		case MULTIPLY:
			return input_value * with_magnitude;
		case OVERRIDE:
			return with_magnitude;
		case ADD:
		default:
			return input_value + with_magnitude;
	}
}

void AttributeModifier::_bind_methods()
{
	BIND_ENUM_CONSTANT(ADD);
	BIND_ENUM_CONSTANT(MULTIPLY);
	BIND_ENUM_CONSTANT(OVERRIDE);

	BIND_ENUM_CONSTANT(SCALABLE_FLOAT);
	BIND_ENUM_CONSTANT(ATTRIBUTE_BASED);
	BIND_ENUM_CONSTANT(SET_BY_CALLER);

	BIND_ENUM_CONSTANT(SOURCE);
	BIND_ENUM_CONSTANT(TARGET);

	ClassDB::bind_method(D_METHOD("get_attribute"), &AttributeModifier::get_attribute);
	ClassDB::bind_method(D_METHOD("set_attribute", "value"), &AttributeModifier::set_attribute);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "attribute"), "set_attribute", "get_attribute");

	ClassDB::bind_method(D_METHOD("get_operation"), &AttributeModifier::get_operation);
	ClassDB::bind_method(D_METHOD("set_operation", "value"), &AttributeModifier::set_operation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "operation", PROPERTY_HINT_ENUM, "Add,Multiply,Override"), "set_operation", "get_operation");

	ClassDB::bind_method(D_METHOD("get_magnitude"), &AttributeModifier::get_magnitude);
	ClassDB::bind_method(D_METHOD("set_magnitude", "value"), &AttributeModifier::set_magnitude);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "magnitude"), "set_magnitude", "get_magnitude");

	ClassDB::bind_method(D_METHOD("get_magnitude_type"), &AttributeModifier::get_magnitude_type);
	ClassDB::bind_method(D_METHOD("set_magnitude_type", "value"), &AttributeModifier::set_magnitude_type);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "magnitude_type", PROPERTY_HINT_ENUM, "Scalable Float,Attribute Based,Set By Caller"), "set_magnitude_type", "get_magnitude_type");

	ADD_GROUP("Scalable Float", "");
	ClassDB::bind_method(D_METHOD("get_level_curve"), &AttributeModifier::get_level_curve);
	ClassDB::bind_method(D_METHOD("set_level_curve", "value"), &AttributeModifier::set_level_curve);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "level_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_level_curve", "get_level_curve");

	ADD_GROUP("Attribute Based", "");
	ClassDB::bind_method(D_METHOD("get_backing_attribute"), &AttributeModifier::get_backing_attribute);
	ClassDB::bind_method(D_METHOD("set_backing_attribute", "value"), &AttributeModifier::set_backing_attribute);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "backing_attribute"), "set_backing_attribute", "get_backing_attribute");

	ClassDB::bind_method(D_METHOD("get_attribute_source"), &AttributeModifier::get_attribute_source);
	ClassDB::bind_method(D_METHOD("set_attribute_source", "value"), &AttributeModifier::set_attribute_source);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "attribute_source", PROPERTY_HINT_ENUM, "Source,Target"), "set_attribute_source", "get_attribute_source");

	ClassDB::bind_method(D_METHOD("get_coefficient"), &AttributeModifier::get_coefficient);
	ClassDB::bind_method(D_METHOD("set_coefficient", "value"), &AttributeModifier::set_coefficient);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "coefficient"), "set_coefficient", "get_coefficient");

	ClassDB::bind_method(D_METHOD("get_pre_multiply_additive"), &AttributeModifier::get_pre_multiply_additive);
	ClassDB::bind_method(D_METHOD("set_pre_multiply_additive", "value"), &AttributeModifier::set_pre_multiply_additive);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pre_multiply_additive"), "set_pre_multiply_additive", "get_pre_multiply_additive");

	ClassDB::bind_method(D_METHOD("get_post_multiply_additive"), &AttributeModifier::get_post_multiply_additive);
	ClassDB::bind_method(D_METHOD("set_post_multiply_additive", "value"), &AttributeModifier::set_post_multiply_additive);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "post_multiply_additive"), "set_post_multiply_additive", "get_post_multiply_additive");

	ADD_GROUP("Set By Caller", "");
	ClassDB::bind_method(D_METHOD("get_set_by_caller_name"), &AttributeModifier::get_set_by_caller_name);
	ClassDB::bind_method(D_METHOD("set_set_by_caller_name", "value"), &AttributeModifier::set_set_by_caller_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "set_by_caller_name"), "set_set_by_caller_name", "get_set_by_caller_name");

	ClassDB::bind_method(D_METHOD("calculate_magnitude", "spec", "target"), &AttributeModifier::calculate_magnitude);
	ClassDB::bind_method(D_METHOD("apply", "input_value"), &AttributeModifier::apply);
	ClassDB::bind_method(D_METHOD("apply_magnitude", "input_value", "with_magnitude"), &AttributeModifier::apply_magnitude);
}
}
