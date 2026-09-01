#ifndef SAVE_GAME_H
#define SAVE_GAME_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

using namespace godot;

namespace GFGD
{
// Base class for save data. By default all script variables are serialized to
// JSON; override _to_json/_from_json in a script for custom serialization.
class SaveGame : public Resource
{
	GDCLASS(SaveGame, Resource)

public:
	SaveGame();
	~SaveGame();

	String to_json();
	Error from_json(const Variant& data);

	GDVIRTUAL0R(String, _to_json)
	GDVIRTUAL1R(int, _from_json, Variant)

protected:
	static void _bind_methods();
};

}

#endif
