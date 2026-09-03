#ifndef PLAYER_VIRTUAL_JOYSTICK_H
#define PLAYER_VIRTUAL_JOYSTICK_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

namespace GFGD
{
class PlayerInput;

// On-screen thumbstick that writes to one player's own PlayerInput, so a pawn
// reads it through its InputComponent like any other action and never has to
// know whether the machine has a touchscreen.
//
// Godot ships its own VirtualJoystick, and for a single-player game that one is
// the better choice: it is themed, and PlayerInput in passthrough forwards to
// Input anyway. This exists for the two things it cannot do.
//
// The first is ownership. The engine's stick writes to the global Input
// singleton, which merges every device - with two people on one machine it moves
// both pawns. This one writes to the PlayerInput of the local player it belongs
// to, which is the whole reason PlayerInput exists.
//
// The second is press_action: an action held for as long as a finger is on the
// stick. That lets one gesture mean both "move" and "hold", which is what a
// game built around a single gesture needs and what a four-axis stick alone
// cannot express.
//
// Touches are tracked BY FINGER INDEX in _input rather than through GUI routing,
// because GUI routing hands a second finger to the same Control and one of the
// two is lost. Events are never marked handled, so buttons above keep working -
// the stick simply ignores anything outside its own rect.
class PlayerVirtualJoystick : public Control
{
	GDCLASS(PlayerVirtualJoystick, Control)

public:
	enum Mode
	{
		// The stick appears wherever the finger lands. This is the one-thumb
		// layout: no fixed corner to reach for, so it works one-handed.
		DYNAMIC = 0,
		// The stick sits at the centre of this control and stays there.
		FIXED = 1,
	};

private:
	StringName action_left;
	StringName action_right;
	StringName action_up;
	StringName action_down;

	// Held for as long as a finger is on the stick. Lets one gesture mean both
	// "move" and "hold", which is what a one-gesture game needs.
	StringName press_action;

	Mode mode;
	float max_distance;
	float deadzone;
	bool recenter_on_overshoot;
	bool draw_default;
	int player_index;

	int finger_index;
	Vector2 origin;
	Vector2 current;
	Vector2 value;

	// The stick only writes to the four actions while a finger is down, and
	// releases them once when it lifts. Writing every frame would fight a
	// keyboard pressing the same actions.
	bool actions_held;

public:
	PlayerVirtualJoystick();
	~PlayerVirtualJoystick();

	virtual void _ready() override;
	virtual void _input(const Ref<InputEvent>& event) override;
	virtual void _process(double delta) override;
	virtual void _draw() override;
	virtual void _notification(int what);

	Vector2 get_value() const { return value; }
	bool is_active() const { return finger_index != -1; }

	// Drops the finger and releases every action the stick is holding. Call it
	// when gameplay input is taken away - pausing, a cutscene, a menu.
	void reset();

	StringName get_action_left() const { return action_left; }
	void set_action_left(const StringName& value) { action_left = value; }

	StringName get_action_right() const { return action_right; }
	void set_action_right(const StringName& value) { action_right = value; }

	StringName get_action_up() const { return action_up; }
	void set_action_up(const StringName& value) { action_up = value; }

	StringName get_action_down() const { return action_down; }
	void set_action_down(const StringName& value) { action_down = value; }

	StringName get_press_action() const { return press_action; }
	void set_press_action(const StringName& value) { press_action = value; }

	Mode get_mode() const { return mode; }
	void set_mode(Mode value) { mode = value; }

	float get_max_distance() const { return max_distance; }
	void set_max_distance(float value) { max_distance = value; }

	float get_deadzone() const { return deadzone; }
	void set_deadzone(float value) { deadzone = value; }

	bool get_recenter_on_overshoot() const { return recenter_on_overshoot; }
	void set_recenter_on_overshoot(bool value) { recenter_on_overshoot = value; }

	bool get_draw_default() const { return draw_default; }
	void set_draw_default(bool value) { draw_default = value; }

	int get_player_index() const { return player_index; }
	void set_player_index(int value) { player_index = value; }

protected:
	static void _bind_methods();

private:
	// Null when there is no local player yet; every write then goes to the global
	// Input singleton, which is also what PlayerInput does in passthrough.
	PlayerInput* resolve_player_input() const;

	void write_action(const StringName& action_name, float strength);
	void release_actions();
	void begin_drag(int index, const Vector2& position);
	void end_drag();
};
}

VARIANT_ENUM_CAST(GFGD::PlayerVirtualJoystick::Mode);

#endif
