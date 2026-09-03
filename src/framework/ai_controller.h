#ifndef AI_CONTROLLER_H
#define AI_CONTROLLER_H

#include "framework/controller.h"

using namespace godot;

namespace GFGD
{
// A controller with nobody behind it. It exists only where the rules are decided
// - on the server - so a pawn it possesses is driven there and mirrored to
// everyone else like any other pawn.
class AIController : public Controller
{
	GDCLASS(AIController, Controller)

public:
	AIController();
	~AIController();

	virtual void _enter_tree() override;

	// Takes over the pawn under pawn_root, whatever node in it the Pawn is.
	bool possess_pawn_root(Node* pawn_root);

protected:
	static void _bind_methods();
};
}

#endif
