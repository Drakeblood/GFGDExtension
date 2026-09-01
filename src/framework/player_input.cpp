#include "framework/player_input.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
PlayerInput::PlayerInput()
{
	passthrough = false;
}

PlayerInput::~PlayerInput()
{

}

int PlayerInput::device_slot_from_event(const Ref<InputEvent>& event)
{
	if (event.is_null()) { return DEVICE_SLOT_NONE; }

	// Classify by event class rather than by the device id alone. Godot reserves
	// InputEvent::DEVICE_ID_KEYBOARD (16) and DEVICE_ID_MOUSE (32), but a joypad
	// is free to report any index and a synthesized InputEventAction reports 0,
	// so the class is the only dependable discriminator.
	if (Object::cast_to<InputEventJoypadButton>(event.ptr()) != nullptr || Object::cast_to<InputEventJoypadMotion>(event.ptr()) != nullptr)
	{
		return event->get_device();
	}

	return DEVICE_SLOT_KEYBOARD_MOUSE;
}

bool PlayerInput::accepts_device_slot(int device_slot) const
{
	if (passthrough) { return true; }
	if (device_slot == DEVICE_SLOT_NONE) { return false; }

	return device_slots.has(device_slot);
}

void PlayerInput::refresh_action_cache()
{
	cached_actions = InputMap::get_singleton()->get_actions();
}

void PlayerInput::feed_input_event(const Ref<InputEvent>& event)
{
	// In passthrough the engine singleton already holds the state; mirroring it
	// here would only risk the two drifting apart.
	if (passthrough) { return; }
	if (event.is_null()) { return; }

	// Mouse motion and friends are not action type, which skips the most frequent
	// events before the per-action scan. An echo repeats a key that is already
	// down, and restamping the press frame would fire STARTED on every repeat.
	if (!event->is_action_type() || event->is_echo()) { return; }

	if (cached_actions.is_empty())
	{
		refresh_action_cache();
	}

	const int device_slot = device_slot_from_event(event);
	Ref<InputEventJoypadMotion> joypad_motion = event;

	for (int i = 0; i < cached_actions.size(); i++)
	{
		StringName action_name = cached_actions[i];
		if (!event->is_action(action_name)) { continue; }

		// is_action_pressed and get_action_strength both run through the InputMap,
		// so the deadzone of the action is applied for us. A stick returning to
		// centre arrives as is_action() true with is_action_pressed() false.
		const bool pressed = event->is_action_pressed(action_name, true);

		ActionState& action_state = action_states[action_name];
		DeviceState& device_state = action_state.device_states[device_slot];

		device_state.pressed = pressed;
		device_state.strength = pressed ? event->get_action_strength(action_name) : 0.0f;

		// Read the raw axis straight off the event rather than trying to undo the
		// deadzone the engine already applied. Buttons and keys are all or nothing.
		//
		// Only when the action is actually pressed: an axis event matches every
		// action bound to that axis, including the one pointing the other way, and
		// giving the opposite action the same magnitude would make get_vector
		// cancel the two out to zero.
		if (!pressed)
		{
			device_state.raw_strength = 0.0f;
		}
		else if (joypad_motion.is_valid())
		{
			device_state.raw_strength = Math::abs(joypad_motion->get_axis_value());
		}
		else
		{
			device_state.raw_strength = 1.0f;
		}

		update_action_state(action_state);
	}
}

void PlayerInput::update_action_state(ActionState& action_state)
{
	const bool was_pressed = action_state.pressed;

	bool any_pressed = action_state.api_pressed;
	float max_strength = action_state.api_strength;
	float max_raw_strength = action_state.api_strength;
	for (const KeyValue<int, DeviceState>& entry : action_state.device_states)
	{
		any_pressed = any_pressed || entry.value.pressed;
		max_strength = Math::max(max_strength, entry.value.strength);
		max_raw_strength = Math::max(max_raw_strength, entry.value.raw_strength);
	}

	action_state.pressed = any_pressed;
	action_state.strength = any_pressed ? max_strength : 0.0f;
	action_state.raw_strength = max_raw_strength;

	if (any_pressed == was_pressed) { return; }

	// Stamping the frame is how the engine Input tells "just pressed" apart from
	// "held". It makes the answer independent of how many listeners ask and in
	// what order their _process runs, and both counters are stamped so a caller
	// in _physics_process gets the same guarantee as one in _process.
	Engine* engine = Engine::get_singleton();
	if (any_pressed)
	{
		action_state.pressed_process_frame = engine->get_process_frames();
		action_state.pressed_physics_frame = engine->get_physics_frames();
	}
	else
	{
		action_state.released_process_frame = engine->get_process_frames();
		action_state.released_physics_frame = engine->get_physics_frames();
	}
}

void PlayerInput::reset_action_states()
{
	for (KeyValue<StringName, ActionState>& entry : action_states)
	{
		entry.value.device_states.clear();
		entry.value.api_pressed = false;
		entry.value.api_strength = 0.0f;

		// Go through update_action_state so a listener waiting on COMPLETED still
		// gets it, instead of the action silently vanishing while held.
		update_action_state(entry.value);
	}
}

const PlayerInput::ActionState* PlayerInput::find_action_state(const StringName& action_name) const
{
	HashMap<StringName, ActionState>::ConstIterator found = action_states.find(action_name);
	return found != action_states.end() ? &found->value : nullptr;
}

bool PlayerInput::is_action_pressed(const StringName& action_name) const
{
	if (passthrough) { return Input::get_singleton()->is_action_pressed(action_name); }

	const ActionState* action_state = find_action_state(action_name);
	return action_state != nullptr && action_state->pressed;
}

bool PlayerInput::is_action_just_pressed(const StringName& action_name) const
{
	if (passthrough) { return Input::get_singleton()->is_action_just_pressed(action_name); }

	const ActionState* action_state = find_action_state(action_name);
	if (action_state == nullptr || !action_state->pressed) { return false; }

	Engine* engine = Engine::get_singleton();
	return engine->is_in_physics_frame()
			? action_state->pressed_physics_frame == engine->get_physics_frames()
			: action_state->pressed_process_frame == engine->get_process_frames();
}

bool PlayerInput::is_action_just_released(const StringName& action_name) const
{
	if (passthrough) { return Input::get_singleton()->is_action_just_released(action_name); }

	const ActionState* action_state = find_action_state(action_name);
	if (action_state == nullptr || action_state->pressed) { return false; }

	Engine* engine = Engine::get_singleton();
	return engine->is_in_physics_frame()
			? action_state->released_physics_frame == engine->get_physics_frames()
			: action_state->released_process_frame == engine->get_process_frames();
}

float PlayerInput::get_action_strength(const StringName& action_name) const
{
	if (passthrough) { return Input::get_singleton()->get_action_strength(action_name); }

	const ActionState* action_state = find_action_state(action_name);
	return action_state != nullptr ? action_state->strength : 0.0f;
}

float PlayerInput::get_action_raw_strength(const StringName& action_name) const
{
	if (passthrough) { return Input::get_singleton()->get_action_raw_strength(action_name); }

	const ActionState* action_state = find_action_state(action_name);
	return action_state != nullptr ? action_state->raw_strength : 0.0f;
}

float PlayerInput::get_axis(const StringName& negative_action, const StringName& positive_action) const
{
	if (passthrough) { return Input::get_singleton()->get_axis(negative_action, positive_action); }

	return get_action_strength(positive_action) - get_action_strength(negative_action);
}

Vector2 PlayerInput::get_vector(const StringName& negative_x, const StringName& positive_x, const StringName& negative_y, const StringName& positive_y, float deadzone) const
{
	if (passthrough) { return Input::get_singleton()->get_vector(negative_x, positive_x, negative_y, positive_y, deadzone); }

	// Built from raw strengths so the circular deadzone below is applied once,
	// not on top of the per-action deadzone the InputMap already applied.
	Vector2 vector(
			get_action_raw_strength(positive_x) - get_action_raw_strength(negative_x),
			get_action_raw_strength(positive_y) - get_action_raw_strength(negative_y));

	if (deadzone < 0.0f)
	{
		// Same rule as Input::get_vector - the average of the four action deadzones.
		InputMap* input_map = InputMap::get_singleton();
		deadzone = 0.25f * (float)(input_map->action_get_deadzone(negative_x) + input_map->action_get_deadzone(positive_x) + input_map->action_get_deadzone(negative_y) + input_map->action_get_deadzone(positive_y));
	}

	const float length = (float)vector.length();
	if (length <= deadzone) { return Vector2(); }
	if (length > 1.0f) { return vector / length; }
	if (deadzone >= 1.0f) { return Vector2(); }

	return vector * ((length - deadzone) / (1.0f - deadzone) / length);
}

void PlayerInput::action_press(const StringName& action_name, float strength)
{
	if (passthrough)
	{
		Input::get_singleton()->action_press(action_name, strength);
		return;
	}

	ActionState& action_state = action_states[action_name];
	action_state.api_pressed = true;
	action_state.api_strength = strength;
	update_action_state(action_state);
}

void PlayerInput::action_release(const StringName& action_name)
{
	if (passthrough)
	{
		Input::get_singleton()->action_release(action_name);
		return;
	}

	ActionState& action_state = action_states[action_name];
	action_state.api_pressed = false;
	action_state.api_strength = 0.0f;
	update_action_state(action_state);
}

void PlayerInput::set_device_slots(const PackedInt32Array& value)
{
	device_slots = value;

	const bool was_passthrough = passthrough;
	passthrough = device_slots.has((int)DEVICE_SLOT_ALL);

	// Leaving passthrough starts from a blank state rather than from whatever the
	// engine singleton happened to hold.
	if (was_passthrough != passthrough)
	{
		action_states.clear();
	}
}

void PlayerInput::add_device_slot(int device_slot)
{
	if (device_slot == DEVICE_SLOT_NONE || device_slots.has(device_slot)) { return; }

	PackedInt32Array new_slots = device_slots;
	new_slots.push_back(device_slot);
	set_device_slots(new_slots);
}

void PlayerInput::remove_device_slot(int device_slot)
{
	const int64_t index = device_slots.find(device_slot);
	if (index < 0) { return; }

	PackedInt32Array new_slots = device_slots;
	new_slots.remove_at(index);
	set_device_slots(new_slots);

	// Nothing will ever deliver the release for a device that is gone, so drop
	// only what that device was contributing and leave the other devices alone.
	for (KeyValue<StringName, ActionState>& entry : action_states)
	{
		if (!entry.value.device_states.has(device_slot)) { continue; }

		entry.value.device_states.erase(device_slot);
		update_action_state(entry.value);
	}
}

void PlayerInput::_bind_methods()
{
	BIND_ENUM_CONSTANT(DEVICE_SLOT_NONE);
	BIND_ENUM_CONSTANT(DEVICE_SLOT_ALL);
	BIND_ENUM_CONSTANT(DEVICE_SLOT_KEYBOARD_MOUSE);

	ClassDB::bind_static_method("PlayerInput", D_METHOD("device_slot_from_event", "event"), &PlayerInput::device_slot_from_event);

	ClassDB::bind_method(D_METHOD("accepts_device_slot", "device_slot"), &PlayerInput::accepts_device_slot);
	ClassDB::bind_method(D_METHOD("feed_input_event", "event"), &PlayerInput::feed_input_event);
	ClassDB::bind_method(D_METHOD("reset_action_states"), &PlayerInput::reset_action_states);
	ClassDB::bind_method(D_METHOD("refresh_action_cache"), &PlayerInput::refresh_action_cache);

	ClassDB::bind_method(D_METHOD("is_action_pressed", "action_name"), &PlayerInput::is_action_pressed);
	ClassDB::bind_method(D_METHOD("is_action_just_pressed", "action_name"), &PlayerInput::is_action_just_pressed);
	ClassDB::bind_method(D_METHOD("is_action_just_released", "action_name"), &PlayerInput::is_action_just_released);
	ClassDB::bind_method(D_METHOD("get_action_strength", "action_name"), &PlayerInput::get_action_strength);
	ClassDB::bind_method(D_METHOD("get_action_raw_strength", "action_name"), &PlayerInput::get_action_raw_strength);
	ClassDB::bind_method(D_METHOD("get_axis", "negative_action", "positive_action"), &PlayerInput::get_axis);
	ClassDB::bind_method(D_METHOD("get_vector", "negative_x", "positive_x", "negative_y", "positive_y", "deadzone"), &PlayerInput::get_vector, DEFVAL(-1.0f));

	ClassDB::bind_method(D_METHOD("action_press", "action_name", "strength"), &PlayerInput::action_press, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("action_release", "action_name"), &PlayerInput::action_release);

	ClassDB::bind_method(D_METHOD("get_device_slots"), &PlayerInput::get_device_slots);
	ClassDB::bind_method(D_METHOD("set_device_slots", "value"), &PlayerInput::set_device_slots);
	ClassDB::bind_method(D_METHOD("add_device_slot", "device_slot"), &PlayerInput::add_device_slot);
	ClassDB::bind_method(D_METHOD("remove_device_slot", "device_slot"), &PlayerInput::remove_device_slot);
	ClassDB::bind_method(D_METHOD("has_device_slot", "device_slot"), &PlayerInput::has_device_slot);
	ClassDB::bind_method(D_METHOD("is_passthrough"), &PlayerInput::is_passthrough);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "device_slots"), "set_device_slots", "get_device_slots");
}
}
