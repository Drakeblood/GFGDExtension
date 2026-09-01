#ifndef INPUT_COMPONENT_H
#define INPUT_COMPONENT_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

namespace GFGD
{
class PlayerController;
class PlayerInput;

class InputComponent : public Node
{
	GDCLASS(InputComponent, Node)

public:
	enum TriggerEvent
	{
		TRIGGERED = 0,	// Fired every frame while the action is held.
		STARTED = 1,	// Fired once when the action is pressed.
		COMPLETED = 2,	// Fired once when the action is released.
	};

private:
	bool enabled;
	Array bindings;

	// Set by PlayerController::register_input_component. It decides which player's
	// devices this component listens to; without it the component falls back to
	// the global Input singleton, which is the pre-local-multiplayer behaviour.
	PlayerController* owning_controller;

public:
	InputComponent();
	~InputComponent();

	void bind_action(const StringName& action_name, TriggerEvent trigger_event, const Callable& action);
	void remove_binding(const StringName& action_name, TriggerEvent trigger_event, const Callable& action);
	void remove_all_bindings();

	// Polled by PlayerController's input pump every frame.
	void process_input(double delta);

	bool is_action_pressed(const StringName& action_name) const;
	bool is_action_just_pressed(const StringName& action_name) const;
	bool is_action_just_released(const StringName& action_name) const;
	float get_action_strength(const StringName& action_name) const;
	float get_axis(const StringName& negative_action, const StringName& positive_action) const;
	Vector2 get_vector(const StringName& negative_x, const StringName& positive_x, const StringName& negative_y, const StringName& positive_y, float deadzone = -1.0f) const;

	bool get_enabled() const { return enabled; }
	void set_enabled(bool value) { enabled = value; }

	PlayerController* get_owning_controller() const { return owning_controller; }
	void set_owning_controller(PlayerController* value) { owning_controller = value; }

	// Null while the component is unregistered or its player has no LocalPlayer,
	// in which case every query falls back to the global Input singleton.
	PlayerInput* get_player_input() const;

protected:
	static void _bind_methods();
};
}

VARIANT_ENUM_CAST(GFGD::InputComponent::TriggerEvent);

#endif
