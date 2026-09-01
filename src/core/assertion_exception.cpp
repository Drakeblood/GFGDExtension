#include "core/assertion_exception.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
AssertionException::AssertionException(const String& message, const String& user_message)
{
	this->failure_message = message;
	this->user_message = user_message;
}

AssertionException::~AssertionException()
{

}

String AssertionException::get_message() const
{
	String text = failure_message;
	if (!user_message.is_empty())
	{
		text = user_message + String("\n") + text;
	}
	return text;
}

void AssertionException::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_message"), &AssertionException::get_message);
}
}
