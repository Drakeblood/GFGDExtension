#ifndef I_PAWN_H
#define I_PAWN_H

#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace GFGD 
{
class Controller;
class InputComponent;

class IPawn : public Node
{
	GDCLASS(IPawn, Node)

public:
	IPawn();
	~IPawn();

	virtual void possessed_by(class Controller* new_controller);
	virtual void unpossessed();
	virtual void setup_input_component(class InputComponent* input_component);

protected:
	static void _bind_methods();
};

}

#endif