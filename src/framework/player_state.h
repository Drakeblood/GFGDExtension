#ifndef PLAYER_STATE_H
#define PLAYER_STATE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

namespace GFGD
{
class World;

// Per-player data that everyone is meant to see. It lives under the GameState
// rather than under the PlayerController on purpose: a controller exists only on
// the server and on its owning client, while this has to be visible to every
// peer - a scoreboard on one client lists players sitting at other machines.
class PlayerState : public Node
{
	GDCLASS(PlayerState, Node)

private:
	String player_name;

	// Handed out by the server and unique for the session, including for a second
	// player sharing one machine. This is what a controller and a pawn are named
	// after, so it is how the same player is found on every peer.
	int player_id;

	// Which human at the owning machine this is: 0 for the first, 1 for the
	// second on a split screen.
	int player_index;

	// The connection this player is on. The server is 1, which is also what a
	// game with no peer reports.
	int unique_id;

	// True on the machine this player is sitting at. Not replicated - it is a
	// different answer on every peer.
	bool local;

	int score;
	bool spectator;
	bool a_bot;

	// Round trip time in seconds, measured by the server.
	float ping;

public:
	PlayerState();
	~PlayerState();

	virtual void _ready() override;

	String get_player_name() const { return player_name; }
	void set_player_name(const String& value);

	int get_player_id() const { return player_id; }
	void set_player_id(int value) { player_id = value; }

	int get_player_index() const { return player_index; }
	void set_player_index(int value) { player_index = value; }

	int get_unique_id() const { return unique_id; }
	void set_unique_id(int value) { unique_id = value; }

	bool is_local() const { return local; }
	void set_local(bool value) { local = value; }

	int get_score() const { return score; }
	void set_score(int value);

	bool is_spectator() const { return spectator; }
	void set_spectator(bool value) { spectator = value; }

	bool is_a_bot() const { return a_bot; }
	void set_a_bot(bool value) { a_bot = value; }

	float get_ping() const { return ping; }
	void set_ping(float value) { ping = value; }

	World* get_world() const;
	bool has_authority() const;
	int get_local_role() const;
	int get_remote_role() const;

	// Names of this node's own properties to keep in step with the server's copy.
	GDVIRTUAL0R(PackedStringArray, _get_replicated_properties)

protected:
	static void _bind_methods();
};
}

#endif
