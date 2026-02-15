#ifndef ASSERTION_EXCEPTION_H
#define ASSERTION_EXCEPTION_H

#include <godot_cpp/classes/object.hpp>

using namespace godot;

namespace GFGD 
{
class AssertionException : public Object
{
	GDCLASS(AssertionException, Object)

private:
	String user_message;

public:
	AssertionException(const String& message, const String& user_message);
	~AssertionException();

	String get_message() const;

protected:
	static void _bind_methods();
};

}

#endif