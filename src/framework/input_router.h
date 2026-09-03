#ifndef INPUT_ROUTER_H
#define INPUT_ROUTER_H

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>

using namespace godot;

namespace GFGD
{
class World;

// Sends every raw InputEvent to the PlayerInput of whichever local player owns
// the device it came from.
//
// It is a single node parented to the root, created once and never freed with a
// level, because:
//   - a node only receives _input for its own nearest ancestor Viewport, so a
//     router at the root sees every event exactly once no matter how many
//     SubViewports a split screen layout adds,
//   - an event from a device nobody owns has no controller to catch it, and
//     press-to-join needs somewhere central to happen,
//   - PlayerControllers are recreated per level, which would leave gaps.
//
// It listens on _input rather than _unhandled_input on purpose. This is a state
// machine: if a Control swallowed a press but not the release, the action would
// stay held forever. The router never marks input handled, so the GUI and every
// _unhandled_input in the project still see events exactly as before.
class InputRouter : public Node
{
	GDCLASS(InputRouter, Node)

private:
	World* world;

public:
	InputRouter();
	~InputRouter();

	virtual void _enter_tree() override;
	virtual void _exit_tree() override;
	virtual void _input(const Ref<InputEvent>& event) override;

	void set_world(World* value) { world = value; }
	World* get_world() const { return world; }

protected:
	static void _bind_methods();

private:
	void on_joy_connection_changed(int32_t device, bool connected);
	bool is_join_worthy(const Ref<InputEvent>& event) const;
};
}

#endif
