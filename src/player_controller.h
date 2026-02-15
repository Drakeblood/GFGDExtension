#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include "controller.h"

using namespace godot;

namespace GFGD 
{
class PlayerController : public Controller
{
	GDCLASS(PlayerController, Controller)

private:
	class InputComponent* input_component;
	Array current_input_stack;
	Array pending_remove_input_component;
	bool scope_lock;

public:
	PlayerController();
	~PlayerController();

	virtual void _enter_tree() override;
	virtual void _process(double delta) override;
	virtual void _input(const Ref<InputEvent>& event) override;

	virtual void on_possess(class PawnHandler* pawn_to_possess) override;
	virtual void on_unpossess() override;

	void set_pawn_camera_node_as_current();

	void register_input_component(class InputComponent* input);
	void unregister_input_component(class InputComponent* input);

protected:
	static void _bind_methods();
};
}

#endif