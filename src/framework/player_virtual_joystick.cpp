#include "framework/player_virtual_joystick.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_screen_drag.hpp>
#include <godot_cpp/classes/input_event_screen_touch.hpp>

#include "framework/world.h"
#include "framework/local_player.h"
#include "framework/player_input.h"

using namespace godot;

namespace GFGD
{
PlayerVirtualJoystick::PlayerVirtualJoystick()
{
	mode = DYNAMIC;
	max_distance = 90.0f;
	deadzone = 0.12f;
	recenter_on_overshoot = true;
	draw_default = true;
	player_index = 0;

	finger_index = -1;
	actions_held = false;
}

PlayerVirtualJoystick::~PlayerVirtualJoystick()
{

}

void PlayerVirtualJoystick::_ready()
{
	// The stick draws its own visuals and reads raw touches, so it must not
	// swallow presses meant for buttons underneath it.
	set_mouse_filter(MOUSE_FILTER_IGNORE);
	set_process(true);
}

void PlayerVirtualJoystick::_notification(int what)
{
	// A stick hidden mid-drag would otherwise leave its actions pressed forever,
	// and the pawn would keep flying in the last direction.
	if (what == NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree())
	{
		reset();
	}
}

void PlayerVirtualJoystick::_input(const Ref<InputEvent>& event)
{
	if (!is_visible_in_tree()) { return; }

	Ref<InputEventScreenTouch> touch = event;
	if (touch.is_valid())
	{
		if (touch->is_pressed())
		{
			if (finger_index == -1 && get_global_rect().has_point(touch->get_position()))
			{
				begin_drag(touch->get_index(), touch->get_position());
			}
		}
		else if (touch->get_index() == finger_index)
		{
			end_drag();
		}
		return;
	}

	Ref<InputEventScreenDrag> drag = event;
	if (drag.is_valid() && drag->get_index() == finger_index)
	{
		current = drag->get_position();
		queue_redraw();
	}
}

void PlayerVirtualJoystick::_process(double delta)
{
	if (finger_index == -1) { return; }

	Vector2 offset = current - origin;
	float length = (float)offset.length();

	if (recenter_on_overshoot && length > max_distance && length > 0.0f)
	{
		// Drag the centre along behind the finger. Without this a long swipe pins
		// the stick and the player loses the sense of which way is which.
		origin = current - offset / length * max_distance;
		offset = current - origin;
		length = max_distance;
	}

	const float magnitude = max_distance > 0.0f ? length / max_distance : 0.0f;
	value = magnitude < deadzone ? Vector2() : (offset / max_distance).limit_length(1.0f);

	const float x = (float)value.x;
	const float y = (float)value.y;

	write_action(action_left, x < 0.0f ? -x : 0.0f);
	write_action(action_right, x > 0.0f ? x : 0.0f);
	write_action(action_up, y < 0.0f ? -y : 0.0f);
	write_action(action_down, y > 0.0f ? y : 0.0f);
	actions_held = true;

	queue_redraw();
}

void PlayerVirtualJoystick::_draw()
{
	if (!draw_default || finger_index == -1) { return; }

	const Vector2 centre = origin - get_global_position();
	const Vector2 knob = centre + (current - origin).limit_length(max_distance);

	draw_circle(centre, max_distance, Color(1.0f, 1.0f, 1.0f, 0.07f));
	draw_arc(centre, max_distance, 0.0f, Math::TAU, 32, Color(1.0f, 1.0f, 1.0f, 0.22f), 2.0f, true);
	draw_circle(knob, max_distance * 0.36f, Color(0.55f, 0.92f, 1.0f, 0.35f));
	draw_arc(knob, max_distance * 0.36f, 0.0f, Math::TAU, 24, Color(0.7f, 0.96f, 1.0f, 0.75f), 2.5f, true);
}

void PlayerVirtualJoystick::reset()
{
	const bool was_active = finger_index != -1;

	finger_index = -1;
	value = Vector2();
	release_actions();
	queue_redraw();

	if (was_active) { emit_signal("drag_ended"); }
}

void PlayerVirtualJoystick::begin_drag(int index, const Vector2& position)
{
	finger_index = index;
	origin = mode == FIXED ? get_global_position() + get_size() * 0.5f : position;
	current = position;

	if (!press_action.is_empty())
	{
		PlayerInput* player_input = resolve_player_input();
		if (player_input != nullptr) { player_input->action_press(press_action, 1.0f); }
		else { Input::get_singleton()->action_press(press_action, 1.0f); }
	}

	queue_redraw();
	emit_signal("drag_started");
}

void PlayerVirtualJoystick::end_drag()
{
	finger_index = -1;
	value = Vector2();
	release_actions();
	queue_redraw();

	emit_signal("drag_ended");
}

void PlayerVirtualJoystick::release_actions()
{
	PlayerInput* player_input = resolve_player_input();
	Input* input = Input::get_singleton();

	if (!press_action.is_empty())
	{
		if (player_input != nullptr) { player_input->action_release(press_action); }
		else { input->action_release(press_action); }
	}

	if (!actions_held) { return; }
	actions_held = false;

	const StringName axes[4] = { action_left, action_right, action_up, action_down };
	for (int i = 0; i < 4; i++)
	{
		if (axes[i].is_empty()) { continue; }

		if (player_input != nullptr) { player_input->action_release(axes[i]); }
		else { input->action_release(axes[i]); }
	}
}

void PlayerVirtualJoystick::write_action(const StringName& action_name, float strength)
{
	if (action_name.is_empty()) { return; }

	PlayerInput* player_input = resolve_player_input();

	if (strength > 0.0f)
	{
		if (player_input != nullptr) { player_input->action_press(action_name, strength); }
		else { Input::get_singleton()->action_press(action_name, strength); }
		return;
	}

	if (player_input != nullptr) { player_input->action_release(action_name); }
	else { Input::get_singleton()->action_release(action_name); }
}

PlayerInput* PlayerVirtualJoystick::resolve_player_input() const
{
	World* world = Object::cast_to<World>(get_tree());
	if (world == nullptr) { return nullptr; }

	LocalPlayer* local_player = world->get_local_player(player_index);
	if (local_player == nullptr) { return nullptr; }

	return local_player->get_player_input();
}

void PlayerVirtualJoystick::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_value"), &PlayerVirtualJoystick::get_value);
	ClassDB::bind_method(D_METHOD("is_active"), &PlayerVirtualJoystick::is_active);
	ClassDB::bind_method(D_METHOD("reset"), &PlayerVirtualJoystick::reset);

	ClassDB::bind_method(D_METHOD("get_action_left"), &PlayerVirtualJoystick::get_action_left);
	ClassDB::bind_method(D_METHOD("set_action_left", "value"), &PlayerVirtualJoystick::set_action_left);
	ClassDB::bind_method(D_METHOD("get_action_right"), &PlayerVirtualJoystick::get_action_right);
	ClassDB::bind_method(D_METHOD("set_action_right", "value"), &PlayerVirtualJoystick::set_action_right);
	ClassDB::bind_method(D_METHOD("get_action_up"), &PlayerVirtualJoystick::get_action_up);
	ClassDB::bind_method(D_METHOD("set_action_up", "value"), &PlayerVirtualJoystick::set_action_up);
	ClassDB::bind_method(D_METHOD("get_action_down"), &PlayerVirtualJoystick::get_action_down);
	ClassDB::bind_method(D_METHOD("set_action_down", "value"), &PlayerVirtualJoystick::set_action_down);
	ClassDB::bind_method(D_METHOD("get_press_action"), &PlayerVirtualJoystick::get_press_action);
	ClassDB::bind_method(D_METHOD("set_press_action", "value"), &PlayerVirtualJoystick::set_press_action);

	ADD_GROUP("Actions", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "action_left"), "set_action_left", "get_action_left");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "action_right"), "set_action_right", "get_action_right");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "action_up"), "set_action_up", "get_action_up");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "action_down"), "set_action_down", "get_action_down");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "press_action"), "set_press_action", "get_press_action");

	ClassDB::bind_method(D_METHOD("get_mode"), &PlayerVirtualJoystick::get_mode);
	ClassDB::bind_method(D_METHOD("set_mode", "value"), &PlayerVirtualJoystick::set_mode);
	ClassDB::bind_method(D_METHOD("get_max_distance"), &PlayerVirtualJoystick::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_max_distance", "value"), &PlayerVirtualJoystick::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_deadzone"), &PlayerVirtualJoystick::get_deadzone);
	ClassDB::bind_method(D_METHOD("set_deadzone", "value"), &PlayerVirtualJoystick::set_deadzone);
	ClassDB::bind_method(D_METHOD("get_recenter_on_overshoot"), &PlayerVirtualJoystick::get_recenter_on_overshoot);
	ClassDB::bind_method(D_METHOD("set_recenter_on_overshoot", "value"), &PlayerVirtualJoystick::set_recenter_on_overshoot);
	ClassDB::bind_method(D_METHOD("get_draw_default"), &PlayerVirtualJoystick::get_draw_default);
	ClassDB::bind_method(D_METHOD("set_draw_default", "value"), &PlayerVirtualJoystick::set_draw_default);
	ClassDB::bind_method(D_METHOD("get_player_index"), &PlayerVirtualJoystick::get_player_index);
	ClassDB::bind_method(D_METHOD("set_player_index", "value"), &PlayerVirtualJoystick::set_player_index);

	ADD_GROUP("Feel", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Dynamic,Fixed"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "8,400,1,or_greater,suffix:px"), "set_max_distance", "get_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "deadzone", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_deadzone", "get_deadzone");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "recenter_on_overshoot"), "set_recenter_on_overshoot", "get_recenter_on_overshoot");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "draw_default"), "set_draw_default", "get_draw_default");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "player_index", PROPERTY_HINT_RANGE, "0,7,1"), "set_player_index", "get_player_index");

	ADD_SIGNAL(MethodInfo("drag_started"));
	ADD_SIGNAL(MethodInfo("drag_ended"));

	BIND_ENUM_CONSTANT(DYNAMIC);
	BIND_ENUM_CONSTANT(FIXED);
}
}
