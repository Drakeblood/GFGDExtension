#ifndef INPUT_COMPONENT_H
#define INPUT_COMPONENT_H

#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace GFGD 
{
class InputComponent : public Node
{
	GDCLASS(InputComponent, Node)

private:
	bool enabled;
	Array bindings;

public:
	InputComponent();
	~InputComponent();

	virtual void bind_action(const StringName& action_name, int trigger_event, const Callable& action);
	virtual void remove_binding(const StringName& action_name, int trigger_event, const Callable& action);
	virtual void remove_binding(const StringName& action_name);
	void remove_all_bindings();

	bool is_action_pressed(const StringName& action_name) const;

	bool get_enabled() const { return enabled; }
	void set_enabled(bool value) { enabled = value; }

protected:
	static void _bind_methods();
};
}

#endif