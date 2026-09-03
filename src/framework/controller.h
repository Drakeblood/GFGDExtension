#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

using namespace godot;

namespace GFGD
{
class Pawn;
class PlayerState;
class World;

// The will behind a pawn. It is not the pawn: possession can move from one pawn
// to another, and a controller outlives the pawn it is driving.
class Controller : public Node
{
	GDCLASS(Controller, Node)

private:
	Pawn* pawn;
	PlayerState* player_state;

	// Handed out by the server, unique for the session, and what the name of this
	// controller and of its pawn are built from.
	int player_id;

public:
	Controller();
	~Controller();

	// Controllers add themselves to the world's controller list here and take
	// themselves out again in _exit_tree. Doing it from the node callbacks rather
	// than from the game mode catches controllers a designer placed in a level by
	// hand, and _exit_tree always runs before the node is deleted - so the list
	// can never hold a freed controller.
	virtual void _enter_tree() override;
	virtual void _exit_tree() override;

	void possess(Pawn* pawn);
	void unpossess();

	Pawn* get_pawn() const { return pawn; }

	// The node the pawn scene is rooted at, which is what a game moves around.
	Node* get_pawn_root() const;

	// Lives under the GameState, not here: a player controller only exists on the
	// server and on its own client, while a player state has to be everywhere.
	PlayerState* get_player_state() const { return player_state; }
	void set_player_state(PlayerState* value);

	int get_player_id() const { return player_id; }
	void set_player_id(int value) { player_id = value; }

	// Null when the project does not use World as its main loop.
	World* get_world() const;

	bool has_authority() const;
	int get_local_role() const;
	int get_remote_role() const;

	// Which connection this controller answers to. Everything driven by the
	// server answers to the server.
	virtual int get_owner_peer_id() const;

	// False here, true on a player controller. It is what tells a bot's pawn from
	// a human's without reaching for a cast.
	virtual bool is_player_controller() const { return false; }

	// True where this controller's decisions are actually made.
	virtual bool is_local_controller() const;

	// The argument is always the possessed Pawn; it is typed as Node here because
	// a virtual's argument types have to be complete, and Pawn includes this file.
	GDVIRTUAL1(_on_possess, Node*)
	GDVIRTUAL0(_on_unpossess)

protected:
	static void _bind_methods();

	// C++ hooks, called before the script virtuals.
	virtual void on_possess(Pawn* pawn);
	virtual void on_unpossess();
};
}

#endif
