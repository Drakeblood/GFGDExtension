#ifndef PLAYER_INPUT_H
#define PLAYER_INPUT_H

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

namespace GFGD
{
// Per-player action state. Owned by a LocalPlayer and fed raw InputEvents by the
// InputRouter, so two players sitting at the same machine can hold different
// actions at the same time.
class PlayerInput : public Object
{
	GDCLASS(PlayerInput, Object)

public:
	enum DeviceSlot
	{
		DEVICE_SLOT_NONE = -2,				// No device assigned yet - the player is waiting to join.
		DEVICE_SLOT_ALL = -1,				// Every device feeds this player. The single player default.
		DEVICE_SLOT_KEYBOARD_MOUSE = 100,	// Keyboard and mouse count as one device.
		// 0 and up are joypad indices, as reported by InputEvent::get_device().
	};

private:
	// One entry per contributing device, so letting go of the pad does not clear
	// an action the keyboard is still holding.
	struct DeviceState
	{
		bool pressed = false;
		float strength = 0.0f;
		float raw_strength = 0.0f;
	};

	struct ActionState
	{
		HashMap<int, DeviceState> device_states;

		bool api_pressed = false;
		float api_strength = 0.0f;

		bool pressed = false;
		float strength = 0.0f;
		float raw_strength = 0.0f;

		uint64_t pressed_process_frame = UINT64_MAX;
		uint64_t pressed_physics_frame = UINT64_MAX;
		uint64_t released_process_frame = UINT64_MAX;
		uint64_t released_physics_frame = UINT64_MAX;
	};

	HashMap<StringName, ActionState> action_states;
	PackedInt32Array device_slots;
	TypedArray<StringName> cached_actions;
	bool passthrough;

public:
	PlayerInput();
	~PlayerInput();

	// KEYBOARD_MOUSE for anything that is not a joypad event, otherwise the joypad
	// index. Classified by event class because device ids are not dependable.
	static int device_slot_from_event(const Ref<InputEvent>& event);

	bool accepts_device_slot(int device_slot) const;
	void feed_input_event(const Ref<InputEvent>& event);

	// Releases everything that is held, firing just_released for it, then forgets
	// the contributing devices. Used when a pad is unplugged, so no button can
	// stay stuck down with nothing left to release it.
	void reset_action_states();

	// The InputMap action list is snapshotted; call this after adding or removing
	// actions at runtime.
	void refresh_action_cache();

	bool is_action_pressed(const StringName& action_name) const;
	bool is_action_just_pressed(const StringName& action_name) const;
	bool is_action_just_released(const StringName& action_name) const;
	float get_action_strength(const StringName& action_name) const;
	float get_action_raw_strength(const StringName& action_name) const;
	float get_axis(const StringName& negative_action, const StringName& positive_action) const;
	Vector2 get_vector(const StringName& negative_x, const StringName& positive_x, const StringName& negative_y, const StringName& positive_y, float deadzone = -1.0f) const;

	// Synthetic input, mirroring Input::action_press. A filtered player does not
	// see the engine singleton, so a game that drives actions from script - or a
	// remapping UI, or a test - needs these.
	void action_press(const StringName& action_name, float strength = 1.0f);
	void action_release(const StringName& action_name);

	PackedInt32Array get_device_slots() const { return device_slots; }
	void set_device_slots(const PackedInt32Array& value);

	void add_device_slot(int device_slot);
	void remove_device_slot(int device_slot);
	bool has_device_slot(int device_slot) const { return device_slots.has(device_slot); }

	// True while DEVICE_SLOT_ALL is held, in which case every query is forwarded
	// to the engine's Input singleton and behaviour is identical to not using
	// PlayerInput at all.
	bool is_passthrough() const { return passthrough; }

protected:
	static void _bind_methods();

private:
	const ActionState* find_action_state(const StringName& action_name) const;
	void update_action_state(ActionState& action_state);
};
}

VARIANT_ENUM_CAST(GFGD::PlayerInput::DeviceSlot);

#endif
