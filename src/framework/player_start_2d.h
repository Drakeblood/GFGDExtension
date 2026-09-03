#ifndef PLAYER_START_2D_H
#define PLAYER_START_2D_H

#include <godot_cpp/classes/marker2d.hpp>
#include <godot_cpp/core/binder_common.hpp>

using namespace godot;

namespace GFGD
{
// Where a player may appear. The game mode hands one out per spawn and does not
// hand the same one out twice while it is claimed; player_start_tag lets a level
// keep separate sets - one per team, or one for the start of a round.
class PlayerStart2D : public Marker2D
{
	GDCLASS(PlayerStart2D, Marker2D)

private:
	StringName player_start_tag;

public:
	PlayerStart2D();
	~PlayerStart2D();

	StringName get_player_start_tag() const { return player_start_tag; }
	void set_player_start_tag(const StringName& value) { player_start_tag = value; }

protected:
	static void _bind_methods();
};
}

#endif
