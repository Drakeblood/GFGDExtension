#ifndef GFGD_STATICS_H
#define GFGD_STATICS_H

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/c_sharp_script.hpp>

using namespace godot;

namespace GFGD 
{
template <typename T>
T* try_create_instance_from(const Ref<Script>& script) 
{
    if (Ref<GDScript> gdScript = Object::cast_to<GDScript>(*script); gdScript.is_valid()) 
	{
        return Object::cast_to<T>((Object*)gdScript->new_());
    }
    else if (Ref<CSharpScript> csharpScript = Object::cast_to<CSharpScript>(*script); csharpScript.is_valid()) 
	{
        return Object::cast_to<T>((Object*)csharpScript->new_());
    }

    return nullptr;
}
}

#endif