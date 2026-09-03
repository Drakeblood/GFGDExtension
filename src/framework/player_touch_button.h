#ifndef PLAYER_TOUCH_BUTTON_H
#define PLAYER_TOUCH_BUTTON_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/texture2d.hpp>

using namespace godot;

namespace GFGD
{
class PlayerInput;

// On-screen button that holds an input action for as long as a finger is on it,
// writing to one player's own PlayerInput rather than to the global Input
// singleton - so with two people on one machine it acts for one of them.
//
// Godot's TouchScreenButton covers the single-player case and covers it well.
// This differs in two ways: it writes to a PlayerInput, and it is a Control, so
// it anchors alongside the rest of the HUD instead of sitting in world space as
// a Node2D. It tracks its finger by index for the same reason
// PlayerVirtualJoystick does.
class PlayerTouchButton : public Control
{
	GDCLASS(PlayerTouchButton, Control)

public:
	enum Shape
	{
		// Hit-tested against a circle inscribed in this control. A round button
		// under a thumb should not react to the corners of its bounding box.
		CIRCLE = 0,
		RECTANGLE = 1,
	};

private:
	StringName action;
	Shape shape;
	bool draw_default;
	int player_index;

	Ref<Texture2D> texture_normal;
	Ref<Texture2D> texture_pressed;

	int finger_index;

public:
	PlayerTouchButton();
	~PlayerTouchButton();

	virtual void _ready() override;
	virtual void _input(const Ref<InputEvent>& event) override;
	virtual void _draw() override;
	virtual void _notification(int what);

	bool is_pressed() const { return finger_index != -1; }

	// Drops the finger and releases the action. Call it when gameplay input is
	// taken away, or the action stays held through the pause.
	void reset();

	StringName get_action() const { return action; }
	void set_action(const StringName& value) { action = value; }

	Shape get_shape() const { return shape; }
	void set_shape(Shape value) { shape = value; }

	bool get_draw_default() const { return draw_default; }
	void set_draw_default(bool value) { draw_default = value; }

	int get_player_index() const { return player_index; }
	void set_player_index(int value) { player_index = value; }

	Ref<Texture2D> get_texture_normal() const { return texture_normal; }
	void set_texture_normal(const Ref<Texture2D>& value) { texture_normal = value; }

	Ref<Texture2D> get_texture_pressed() const { return texture_pressed; }
	void set_texture_pressed(const Ref<Texture2D>& value) { texture_pressed = value; }

protected:
	static void _bind_methods();

private:
	PlayerInput* resolve_player_input() const;
	bool hit_test(const Vector2& global_point) const;
	void press();
	void release();
};
}

VARIANT_ENUM_CAST(GFGD::PlayerTouchButton::Shape);

#endif
