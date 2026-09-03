#ifndef GFGD_STATICS_H
#define GFGD_STATICS_H

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>

using namespace godot;

namespace GFGD
{
// Returns an unreferenced raw pointer, so T must NOT be RefCounted: a Variant
// holding a RefCounted owns a reference, and the pointer would dangle as soon as
// the temporary below died. Intended for GameInstance (Object) and GameMode
// (Node); a Resource needs its own Ref-taking path.
template <typename T>
T* try_create_instance_from(const Ref<Script>& script)
{
    if (script.is_null()) { return nullptr; }

    if (Ref<GDScript> gd_script = script; gd_script.is_valid())
    {
        return Object::cast_to<T>((Object*)gd_script->new_());
    }

    // Generic path (C# and other script languages): instantiate the script's
    // base type and attach the script to it.
    StringName base_type = script->get_instance_base_type();
    if (base_type == StringName()) { return nullptr; }

    Variant instance = ClassDBSingleton::get_singleton()->instantiate(base_type);
    Object* object = instance;
    T* typed_object = Object::cast_to<T>(object);
    if (typed_object == nullptr)
    {
        if (object != nullptr && Object::cast_to<RefCounted>(object) == nullptr)
        {
            memdelete(object);
        }
        return nullptr;
    }

    typed_object->set_script(script);
    return typed_object;
}
}

#endif
