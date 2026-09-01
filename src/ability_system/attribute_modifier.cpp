#include "ability_system/attribute_modifier.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
AttributeModifier::AttributeModifier()
{
	operation = ADD;
	magnitude = 0.0;
}

double AttributeModifier::apply(double input_value) const
{
	switch (operation)
	{
		case MULTIPLY:
			return input_value * magnitude;
		case OVERRIDE:
			return magnitude;
		case ADD:
		default:
			return input_value + magnitude;
	}
}

void AttributeModifier::_bind_methods()
{
	BIND_ENUM_CONSTANT(ADD);
	BIND_ENUM_CONSTANT(MULTIPLY);
	BIND_ENUM_CONSTANT(OVERRIDE);

	ClassDB::bind_method(D_METHOD("get_attribute"), &AttributeModifier::get_attribute);
	ClassDB::bind_method(D_METHOD("set_attribute", "value"), &AttributeModifier::set_attribute);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "attribute"), "set_attribute", "get_attribute");

	ClassDB::bind_method(D_METHOD("get_operation"), &AttributeModifier::get_operation);
	ClassDB::bind_method(D_METHOD("set_operation", "value"), &AttributeModifier::set_operation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "operation", PROPERTY_HINT_ENUM, "Add,Multiply,Override"), "set_operation", "get_operation");

	ClassDB::bind_method(D_METHOD("get_magnitude"), &AttributeModifier::get_magnitude);
	ClassDB::bind_method(D_METHOD("set_magnitude", "value"), &AttributeModifier::set_magnitude);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "magnitude"), "set_magnitude", "get_magnitude");

	ClassDB::bind_method(D_METHOD("apply", "input_value"), &AttributeModifier::apply);
}
}
