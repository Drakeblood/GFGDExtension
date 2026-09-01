#ifndef ASSERT_H
#define ASSERT_H

#include <godot_cpp/classes/object.hpp>

using namespace godot;

namespace GFGD 
{
class AssertionException;
class AssertionMessages;

class Assert : public Object
{
	GDCLASS(Assert, Object)

public:
	static void is_true(bool condition, const String& message = "");
	static void is_false(bool condition, const String& message = "");
	static void are_equal(const Variant& expected, const Variant& actual, const String& message = "");
	static void are_not_equal(const Variant& expected, const Variant& actual, const String& message = "");
	static void is_null(const Variant& value, const String& message = "");
	static void is_not_null(const Variant& value, const String& message = "");

protected:
	static void _bind_methods();
};

}

#endif