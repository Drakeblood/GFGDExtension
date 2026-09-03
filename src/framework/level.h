#ifndef LEVEL_H
#define LEVEL_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "framework/world.h"

using namespace godot;

namespace GFGD
{

// The root of a playable scene. Everything a level holds is authored under it;
// what it adds on top of a plain node is the right to say which rules apply here.
class Level : public Node
{
	GDCLASS(Level, Node)

private:
	// The game mode to run in this level, in place of the project's default. It
	// is a scene rather than a script so that the default scenes the game mode
	// declares can be set on it in the inspector.
	Ref<PackedScene> game_mode_override;

public:
	Level();
	~Level();

	void init_level(World* world);

	Ref<PackedScene> get_game_mode_override() const { return game_mode_override; }
	void set_game_mode_override(const Ref<PackedScene>& value) { game_mode_override = value; }

	GDVIRTUAL1(_init_level, World*)

protected:
	static void _bind_methods();

};
}

#endif
