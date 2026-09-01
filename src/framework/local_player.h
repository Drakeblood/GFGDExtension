#ifndef LOCAL_PLAYER_H
#define LOCAL_PLAYER_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include "framework/player_input.h"

using namespace godot;

namespace GFGD
{
class PlayerController;

// One human sitting at this machine. Owned by the GameInstance rather than by
// the GameMode, so it survives a level change: the PlayerController dies with
// the old level, the LocalPlayer does not.
class LocalPlayer : public Object
{
	GDCLASS(LocalPlayer, Object)

private:
	int player_index;
	PlayerInput* player_input;
	Viewport* viewport;

	// Valid only for the level that is currently loaded. Cleared by open_level
	// before the old game mode is freed, so it never dangles.
	PlayerController* player_controller;

public:
	LocalPlayer();
	~LocalPlayer();

	int get_player_index() const { return player_index; }
	void set_player_index(int value) { player_index = value; }

	PlayerInput* get_player_input() const { return player_input; }

	PackedInt32Array get_device_slots() const;
	void set_device_slots(const PackedInt32Array& value);
	void add_device_slot(int device_slot);
	void remove_device_slot(int device_slot);
	bool has_device_slot(int device_slot) const;

	// Left null for a shared screen, which is the default. Set it to a SubViewport
	// to give this player its own slice of a split screen.
	Viewport* get_viewport_override() const { return viewport; }
	void set_viewport_override(Viewport* value) { viewport = value; }

	// Moves a camera into this player's viewport and makes it current there.
	// The camera stops following the pawn by parenting - use a RemoteTransform2D
	// or RemoteTransform3D on the pawn to drive it.
	void adopt_camera(Node* camera);

	PlayerController* get_player_controller() const { return player_controller; }
	void set_player_controller(PlayerController* value) { player_controller = value; }

protected:
	static void _bind_methods();
};
}

#endif
