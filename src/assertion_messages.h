#ifndef ASSERTION_MESSAGES_H
#define ASSERTION_MESSAGES_H

#include <godot_cpp/classes/object.hpp>

using namespace godot;

namespace GFGD 
{
class AssertionMessages : public Object
{
	GDCLASS(AssertionMessages, Object)

public:
	static String get_message(const String& failure_message);
	static String get_message(const String& failure_message, const String& expected);
	static String get_equality_message(const Variant& actual, const Variant& expected, bool expect_equal);
	static String null_failure_message(const Variant& value, bool expect_null);
	static String boolean_failure_message(bool expected);

protected:
	static void _bind_methods();
};

}

#endif