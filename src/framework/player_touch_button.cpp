#include "framework/player_touch_button.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_screen_touch.hpp>

#include "framework/world.h"
#include "framework/local_player.h"
#include "framework/player_input.h"

using namespace godot;

namespace GFGD
{
PlayerTouchButton::PlayerTouchButton()
{
	shape = CIRCLE;
	draw_default = true;
	player_index = 0;
	finger_index = -1;
}

PlayerTouchButton::~PlayerTouchButton()
{

}

void PlayerTouchButton::_ready()
{
	// Raw touches are read in _input, so this must not consume presses meant for
	// whatever sits underneath.
	set_mouse_filter(MOUSE_FILTER_IGNORE);
}

void PlayerTouchButton::_notification(int what)
{
	if (what == NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree())
	{
		reset();
	}
}

void PlayerTouchButton::_input(const Ref<InputEvent>& event)
{
	if (!is_visible_in_tree()) { return; }

	Ref<InputEventScreenTouch> touch = event;
	if (touch.is_null()) { return; }

	if (touch->is_pressed())
	{
		if (finger_index == -1 && hit_test(touch->get_position()))
		{
			finger_index = touch->get_index();
			press();
		}
		return;
	}

	if (touch->get_index() == finger_index)
	{
		finger_index = -1;
		release();
	}
}

void PlayerTouchButton::_draw()
{
	const Ref<Texture2D> texture = is_pressed() && texture_pressed.is_valid() ? texture_pressed : texture_normal;
	if (texture.is_valid())
	{
		draw_texture_rect(texture, Rect2(Vector2(), get_size()), false);
		return;
	}

	if (!draw_default) { return; }

	const Vector2 centre = get_size() * 0.5f;
	const Vector2 size = get_size();
	const float shortest = (float)(size.x < size.y ? size.x : size.y);
	const float radius = shortest * 0.5f * (is_pressed() ? 1.08f : 1.0f);

	if (shape == CIRCLE)
	{
		draw_circle(centre, radius, Color(0.55f, 0.92f, 1.0f, is_pressed() ? 0.28f : 0.14f));
		draw_arc(centre, radius, 0.0f, Math::TAU, 40, Color(0.7f, 0.96f, 1.0f, 0.7f), 3.0f, true);
		return;
	}

	draw_rect(Rect2(Vector2(), get_size()), Color(0.55f, 0.92f, 1.0f, is_pressed() ? 0.28f : 0.14f), true);
	draw_rect(Rect2(Vector2(), get_size()), Color(0.7f, 0.96f, 1.0f, 0.7f), false, 3.0f);
}

void PlayerTouchButton::reset()
{
	if (finger_index == -1) { return; }

	finger_index = -1;
	release();
}

bool PlayerTouchButton::hit_test(const Vector2& global_point) const
{
	const Rect2 rect = get_global_rect();
	if (shape == RECTANGLE) { return rect.has_point(global_point); }

	const Vector2 centre = rect.position + rect.size * 0.5f;
	const float radius = (float)(rect.size.x < rect.size.y ? rect.size.x : rect.size.y) * 0.5f;

	return centre.distance_squared_to(global_point) <= radius * radius;
}

void PlayerTouchButton::press()
{
	if (!action.is_empty())
	{
		PlayerInput* player_input = resolve_player_input();
		if (player_input != nullptr) { player_input->action_press(action, 1.0f); }
		else { Input::get_singleton()->action_press(action, 1.0f); }
	}

	queue_redraw();
	emit_signal("pressed");
}

void PlayerTouchButton::release()
{
	if (!action.is_empty())
	{
		PlayerInput* player_input = resolve_player_input();
		if (player_input != nullptr) { player_input->action_release(action); }
		else { Input::get_singleton()->action_release(action); }
	}

	queue_redraw();
	emit_signal("released");
}

PlayerInput* PlayerTouchButton::resolve_player_input() const
{
	World* world = Object::cast_to<World>(get_tree());
	if (world == nullptr) { return nullptr; }

	LocalPlayer* local_player = world->get_local_player(player_index);
	if (local_player == nullptr) { return nullptr; }

	return local_player->get_player_input();
}

void PlayerTouchButton::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("is_pressed"), &PlayerTouchButton::is_pressed);
	ClassDB::bind_method(D_METHOD("reset"), &PlayerTouchButton::reset);

	ClassDB::bind_method(D_METHOD("get_action"), &PlayerTouchButton::get_action);
	ClassDB::bind_method(D_METHOD("set_action", "value"), &PlayerTouchButton::set_action);
	ClassDB::bind_method(D_METHOD("get_shape"), &PlayerTouchButton::get_shape);
	ClassDB::bind_method(D_METHOD("set_shape", "value"), &PlayerTouchButton::set_shape);
	ClassDB::bind_method(D_METHOD("get_draw_default"), &PlayerTouchButton::get_draw_default);
	ClassDB::bind_method(D_METHOD("set_draw_default", "value"), &PlayerTouchButton::set_draw_default);
	ClassDB::bind_method(D_METHOD("get_player_index"), &PlayerTouchButton::get_player_index);
	ClassDB::bind_method(D_METHOD("set_player_index", "value"), &PlayerTouchButton::set_player_index);
	ClassDB::bind_method(D_METHOD("get_texture_normal"), &PlayerTouchButton::get_texture_normal);
	ClassDB::bind_method(D_METHOD("set_texture_normal", "value"), &PlayerTouchButton::set_texture_normal);
	ClassDB::bind_method(D_METHOD("get_texture_pressed"), &PlayerTouchButton::get_texture_pressed);
	ClassDB::bind_method(D_METHOD("set_texture_pressed", "value"), &PlayerTouchButton::set_texture_pressed);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "action"), "set_action", "get_action");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape", PROPERTY_HINT_ENUM, "Circle,Rectangle"), "set_shape", "get_shape");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "draw_default"), "set_draw_default", "get_draw_default");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "player_index", PROPERTY_HINT_RANGE, "0,7,1"), "set_player_index", "get_player_index");

	ADD_GROUP("Textures", "texture_");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture_normal", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture_normal", "get_texture_normal");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture_pressed", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture_pressed", "get_texture_pressed");

	ADD_SIGNAL(MethodInfo("pressed"));
	ADD_SIGNAL(MethodInfo("released"));

	BIND_ENUM_CONSTANT(CIRCLE);
	BIND_ENUM_CONSTANT(RECTANGLE);
}
}
