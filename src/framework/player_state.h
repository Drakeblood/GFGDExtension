#ifndef PLAYER_STATE_H
#define PLAYER_STATE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>

using namespace godot;

namespace GFGD
{
// Per-player data that everyone is meant to see. It lives under the GameState
// rather than under the PlayerController on purpose: a PlayerController exists
// only on the server and on its owning client, while a PlayerState has to be
// visible to every peer.
// Every field is a property so a MultiplayerSynchronizer can pick it up once
// replication is added.
class PlayerState : public Node
{
	GDCLASS(PlayerState, Node)

private:
	String player_name;
	int player_index;

	// The multiplayer peer id once there is networking. 1 is the server, which is
	// also what a purely local game reports.
	int unique_id;
	bool local;
	float ping;

public:
	PlayerState();
	~PlayerState();

	String get_player_name() const { return player_name; }
	void set_player_name(const String& value);

	int get_player_index() const { return player_index; }
	void set_player_index(int value) { player_index = value; }

	int get_unique_id() const { return unique_id; }
	void set_unique_id(int value) { unique_id = value; }

	bool is_local() const { return local; }
	void set_local(bool value) { local = value; }

	// Always 0 without networking. The field exists so UI written today keeps
	// working once replication fills it in.
	float get_ping() const { return ping; }
	void set_ping(float value) { ping = value; }

protected:
	static void _bind_methods();
};
}

#endif
