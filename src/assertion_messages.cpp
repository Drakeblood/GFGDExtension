#include "assertion_messages.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
String AssertionMessages::get_message(const String& failure_message)
{
	return String("Assertion failure: ") + String(failure_message);
}

String AssertionMessages::get_message(const String& failure_message, const String& expected)
{
	return get_message(String(failure_message) + String(" Expected: ") + String(expected));
}

String AssertionMessages::get_equality_message(const Variant& actual, const Variant& expected, bool expect_equal)
{
	return get_message(String("Values are ") + String(expect_equal ? "not " : "") + String("equal."), 
		String(actual) + String(" ") + String(expect_equal ? "==" : "!=") + String(" ") + String(expected));
}

String AssertionMessages::null_failure_message(const Variant& value, bool expect_null)
{
	return get_message(String("Value was ") + String(expect_null ? "not " : "") + String("Null"),
		String("Value was ") + String(expect_null ? "" : "not ") + String("Null"));
}

String AssertionMessages::boolean_failure_message(bool expected)
{
	return get_message(String("Value was ") + String(!expected ? "" : "not ") + String("true"), String(expected ? "true" : "false"));
}

void AssertionMessages::_bind_methods()
{
	ClassDB::bind_static_method("AssertionMessages", D_METHOD("get_message", "failure_message"), &AssertionMessages::get_message);
	ClassDB::bind_static_method("AssertionMessages", D_METHOD("get_message_with_expected", "failure_message", "expected"), &AssertionMessages::get_message);
	ClassDB::bind_static_method("AssertionMessages", D_METHOD("get_equality_message", "actual", "expected", "expect_equal"), &AssertionMessages::get_equality_message);
	ClassDB::bind_static_method("AssertionMessages", D_METHOD("null_failure_message", "value", "expect_null"), &AssertionMessages::null_failure_message);
	ClassDB::bind_static_method("AssertionMessages", D_METHOD("boolean_failure_message", "expected"), &AssertionMessages::boolean_failure_message);
}
}