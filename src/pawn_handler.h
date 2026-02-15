#ifndef PAWN_HANDLER_H
#define PAWN_HANDLER_H

#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace GFGD 
{
class Controller;
class InputComponent;

class PawnHandler : public Node
{
	GDCLASS(PawnHandler, Node)

private:
	Controller* controller;
	InputComponent* input_component;

public:
	PawnHandler();
	~PawnHandler();

	virtual void possessed_by(Controller* new_controller);
	virtual void unpossessed();

	Controller* get_controller() const { return controller; }

protected:
	static void _bind_methods();
};
}

#endif