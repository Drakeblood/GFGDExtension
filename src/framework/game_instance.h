#ifndef GAME_INSTANCE_H
#define GAME_INSTANCE_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include "framework/world.h"

using namespace godot;

namespace GFGD
{
class LocalPlayer;

class GameInstance : public Object
{
	GDCLASS(GameInstance, Object)

private:
	World* world;

	// The humans at this machine. Owned here rather than by the game mode so they
	// outlive a level change.
	Array local_players;

public:
	GameInstance();
	~GameInstance();

	void init(World* world);
	void shutdown();

	World* get_world() const { return world; }

	// device_slot is a PlayerInput.DeviceSlot value: a joypad index, KEYBOARD_MOUSE,
	// ALL for "every device", or NONE to wait for a press-to-join.
	LocalPlayer* create_local_player(int device_slot);
	LocalPlayer* create_local_player_with_slots(const PackedInt32Array& device_slots);
	void remove_local_player(LocalPlayer* local_player);

	LocalPlayer* get_local_player(int index) const;
	LocalPlayer* find_local_player_for_device_slot(int device_slot) const;
	Array get_local_players() const { return local_players; }
	int get_local_player_count() const { return local_players.size(); }

	GDVIRTUAL1(_on_init, World*)
	GDVIRTUAL0(_on_shutdown)

protected:
	static void _bind_methods();

};
}

#endif
