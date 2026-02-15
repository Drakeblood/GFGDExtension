#include "assert.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/ref.hpp>

#include "assertion_exception.h"
#include "assertion_messages.h"

using namespace godot;

namespace GFGD
{
void Assert::is_true(bool condition, const String& message)
{
	if (!condition)
	{
		String failure_message = AssertionMessages::boolean_failure_message(true);
		Ref<AssertionException> exception = memnew(AssertionException(failure_message, message));
		ERR_FAIL_COND_MSG(true, exception->get_message());
	}
}

void Assert::is_false(bool condition, const String& message)
{
	if (condition)
	{
		String failure_message = AssertionMessages::boolean_failure_message(false);
		Ref<AssertionException> exception = memnew(AssertionException(failure_message, message));
		ERR_FAIL_COND_MSG(true, exception->get_message());
	}
}

void Assert::are_equal(const Variant& expected, const Variant& actual, const String& message)
{
	if (actual != expected)
	{
		String failure_message = AssertionMessages::get_equality_message(actual, expected, true);
		Ref<AssertionException> exception = memnew(AssertionException(failure_message, message));
		ERR_FAIL_COND_MSG(true, exception->get_message());
	}
}

void Assert::are_not_equal(const Variant& expected, const Variant& actual, const String& message)
{
	if (actual == expected)
	{
		String failure_message = AssertionMessages::get_equality_message(actual, expected, false);
		Ref<AssertionException> exception = memnew(AssertionException(failure_message, message));
		ERR_FAIL_COND_MSG(true, exception->get_message());
	}
}

void Assert::is_null(const Variant& value, const String& message)
{
	if (value != Variant())
	{
		String failure_message = AssertionMessages::null_failure_message(value, true);
		Ref<AssertionException> exception = memnew(AssertionException(failure_message, message));
		ERR_FAIL_COND_MSG(true, exception->get_message());
	}
}

void Assert::is_not_null(const Variant& value, const String& message)
{
	if (value == Variant())
	{
		String failure_message = AssertionMessages::null_failure_message(value, false);
		Ref<AssertionException> exception = memnew(AssertionException(failure_message, message));
		ERR_FAIL_COND_MSG(true, exception->get_message());
	}
}

void Assert::_bind_methods()
{
	ClassDB::bind_static_method("Assert", D_METHOD("is_true", "condition", "message"), &Assert::is_true);
	ClassDB::bind_static_method("Assert", D_METHOD("is_false", "condition", "message"), &Assert::is_false);
	ClassDB::bind_static_method("Assert", D_METHOD("are_equal", "expected", "actual", "message"), &Assert::are_equal);
	ClassDB::bind_static_method("Assert", D_METHOD("are_not_equal", "expected", "actual", "message"), &Assert::are_not_equal);
	ClassDB::bind_static_method("Assert", D_METHOD("is_null", "value", "message"), &Assert::is_null);
	ClassDB::bind_static_method("Assert", D_METHOD("is_not_null", "value", "message"), &Assert::is_not_null);
}
}