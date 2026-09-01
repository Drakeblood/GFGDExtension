#ifndef ATTRIBUTE_MODIFIER_H
#define ATTRIBUTE_MODIFIER_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>

using namespace godot;

namespace GFGD
{
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

private:
	StringName attribute;
	Operation operation;
	double magnitude;

public:
	AttributeModifier();

	StringName get_attribute() const { return attribute; }
	void set_attribute(const StringName& value) { attribute = value; }

	Operation get_operation() const { return operation; }
	void set_operation(Operation value) { operation = value; }

	double get_magnitude() const { return magnitude; }
	void set_magnitude(double value) { magnitude = value; }

	double apply(double input_value) const;

protected:
	static void _bind_methods();
};

}

VARIANT_ENUM_CAST(GFGD::AttributeModifier::Operation);

#endif
