#ifndef ATTRIBUTE_SET_H
#define ATTRIBUTE_SET_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/typed_dictionary.hpp>

using namespace godot;

namespace GFGD
{
// Data-driven attribute storage (name -> value). Base values are authored in the
// editor; current values are derived at runtime (base + gameplay effect modifiers).
// Extend in GDScript to clamp values (_pre_attribute_change) or react to changes
// (_post_attribute_change); from C# consume the attribute_changed signals instead.
class AttributeSet : public Resource
{
	GDCLASS(AttributeSet, Resource)

private:
	TypedDictionary<StringName, double> attributes;
	Dictionary current_values;

public:
	AttributeSet();

	void define_attribute(const StringName& attribute_name, double base_value);
	bool has_attribute(const StringName& attribute_name) const;

	double get_base_value(const StringName& attribute_name) const;
	void set_base_value(const StringName& attribute_name, double value);

	double get_current_value(const StringName& attribute_name) const;
	void set_current_value(const StringName& attribute_name, double value);

	PackedStringArray get_attribute_names() const;
	void init_current_from_base();

	void set_attributes(const TypedDictionary<StringName, double>& value) { attributes = value; }
	TypedDictionary<StringName, double> get_attributes() const { return attributes; }

	GDVIRTUAL2R(double, _pre_attribute_change, StringName, double)
	GDVIRTUAL3(_post_attribute_change, StringName, double, double)

protected:
	static void _bind_methods();
};

}

#endif
