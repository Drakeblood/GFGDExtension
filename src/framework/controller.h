#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

using namespace godot;

namespace GFGD
{
class GFGDSceneTree;
class PawnHandler;

class Controller : public Node
{
	GDCLASS(Controller, Node)

private:
	PawnHandler* pawn_handler;

public:
	Controller();
	~Controller();

	// Controllers add themselves to the world's controller list here and take
	// themselves out again in _exit_tree. Doing it from the node callbacks rather
	// than from the game mode catches AI controllers and controllers a designer
	// placed in a level by hand, and _exit_tree always runs before the node is
	// deleted - so the list can never hold a freed controller.
	virtual void _enter_tree() override;
	virtual void _exit_tree() override;

	void possess(PawnHandler* pawn_handler);
	void unpossess();

	PawnHandler* get_pawn_handler() const { return pawn_handler; }

	// Null when the project does not use GFGDSceneTree as its main loop.
	GFGDSceneTree* get_gfgd_scene_tree() const;

	// The argument is always the possessed PawnHandler (Node* to avoid a circular include).
	GDVIRTUAL1(_on_possess, Node*)
	GDVIRTUAL0(_on_unpossess)

protected:
	static void _bind_methods();

	// C++ hooks, called before the script virtuals.
	virtual void on_possess(PawnHandler* pawn_handler);
	virtual void on_unpossess();
};
}

#endif
