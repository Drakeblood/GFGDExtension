#include "ability_system/attribute_set.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
AttributeSet::AttributeSet()
{

}

void AttributeSet::define_attribute(const StringName& attribute_name, double base_value)
{
	attributes[attribute_name] = base_value;
}

bool AttributeSet::has_attribute(const StringName& attribute_name) const
{
	return attributes.has(attribute_name);
}

double AttributeSet::get_base_value(const StringName& attribute_name) const
{
	return attributes.get(attribute_name, 0.0);
}

void AttributeSet::set_base_value(const StringName& attribute_name, double value)
{
	double old_value = get_base_value(attribute_name);
	if (old_value == value && attributes.has(attribute_name)) { return; }

	attributes[attribute_name] = value;
	emit_signal("base_value_changed", attribute_name, old_value, value);
}

double AttributeSet::get_current_value(const StringName& attribute_name) const
{
	if (current_values.has(attribute_name))
	{
		return current_values.get(attribute_name, 0.0);
	}
	return get_base_value(attribute_name);
}

void AttributeSet::set_current_value(const StringName& attribute_name, double value)
{
	double clamped_value = value;
	GDVIRTUAL_CALL(_pre_attribute_change, attribute_name, value, clamped_value);

	double old_value = get_current_value(attribute_name);
	if (old_value == clamped_value && current_values.has(attribute_name)) { return; }

	current_values[attribute_name] = clamped_value;
	emit_signal("attribute_changed", attribute_name, old_value, clamped_value);
	GDVIRTUAL_CALL(_post_attribute_change, attribute_name, old_value, clamped_value);
}

PackedStringArray AttributeSet::get_attribute_names() const
{
	PackedStringArray names;
	Array keys = attributes.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		names.push_back(keys[i]);
	}
	return names;
}

void AttributeSet::init_current_from_base()
{
	current_values = attributes.duplicate();
}

void AttributeSet::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("define_attribute", "attribute_name", "base_value"), &AttributeSet::define_attribute);
	ClassDB::bind_method(D_METHOD("has_attribute", "attribute_name"), &AttributeSet::has_attribute);
	ClassDB::bind_method(D_METHOD("get_base_value", "attribute_name"), &AttributeSet::get_base_value);
	ClassDB::bind_method(D_METHOD("set_base_value", "attribute_name", "value"), &AttributeSet::set_base_value);
	ClassDB::bind_method(D_METHOD("get_current_value", "attribute_name"), &AttributeSet::get_current_value);
	ClassDB::bind_method(D_METHOD("set_current_value", "attribute_name", "value"), &AttributeSet::set_current_value);
	ClassDB::bind_method(D_METHOD("get_attribute_names"), &AttributeSet::get_attribute_names);
	ClassDB::bind_method(D_METHOD("init_current_from_base"), &AttributeSet::init_current_from_base);

	ClassDB::bind_method(D_METHOD("set_attributes", "value"), &AttributeSet::set_attributes);
	ClassDB::bind_method(D_METHOD("get_attributes"), &AttributeSet::get_attributes);
	PropertyInfo attributes_info = GetTypeInfo<TypedDictionary<StringName, double>>::get_class_info();
	attributes_info.name = "attributes";
	ADD_PROPERTY(attributes_info, "set_attributes", "get_attributes");

	GDVIRTUAL_BIND(_pre_attribute_change, "attribute_name", "new_value");
	GDVIRTUAL_BIND(_post_attribute_change, "attribute_name", "old_value", "new_value");

	ADD_SIGNAL(MethodInfo("attribute_changed",
			PropertyInfo(Variant::STRING_NAME, "attribute_name"),
			PropertyInfo(Variant::FLOAT, "old_value"),
			PropertyInfo(Variant::FLOAT, "new_value")));
	ADD_SIGNAL(MethodInfo("base_value_changed",
			PropertyInfo(Variant::STRING_NAME, "attribute_name"),
			PropertyInfo(Variant::FLOAT, "old_value"),
			PropertyInfo(Variant::FLOAT, "new_value")));
}
}
