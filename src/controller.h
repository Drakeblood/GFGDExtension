#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace GFGD 
{
class PawnHandler;

class Controller : public Node
{
	GDCLASS(Controller, Node)

private:
	PawnHandler* pawn_handler;

public:
	Controller();
	~Controller();

	virtual void set_pawn_handler(PawnHandler* pawn_handler);
	void possess(PawnHandler* pawn_handler);
	void unpossess();

	virtual void on_possess(PawnHandler* pawn_handler);
	virtual void on_unpossess();

	PawnHandler* get_pawn_handler() const { return pawn_handler; }

protected:
	static void _bind_methods();
};
}

#endif