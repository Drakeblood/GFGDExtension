#ifndef ASSERTION_EXCEPTION_H
#define ASSERTION_EXCEPTION_H

#include <godot_cpp/classes/ref_counted.hpp>

using namespace godot;

namespace GFGD
{
class AssertionException : public RefCounted
{
	GDCLASS(AssertionException, RefCounted)

private:
	String failure_message;
	String user_message;

public:
	AssertionException() { }
	AssertionException(const String& message, const String& user_message);
	~AssertionException();

	String get_message() const;

protected:
	static void _bind_methods();
};

}

#endif
