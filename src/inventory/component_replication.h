#ifndef GFGD_COMPONENT_REPLICATION_H
#define GFGD_COMPONENT_REPLICATION_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

namespace GFGD
{
class World;

// Shared plumbing of the inventory, quick bar and equipment components. Each of
// them keeps the server's copy as the truth and sends a mirror to some peers:
// the owner only (a backpack nobody else should read) or every peer (what a
// character visibly carries). A client asks for the whole state from _ready,
// which proves it has the node; after that the server sends changes, batched
// once per frame.
namespace ComponentReplication
{
enum Audience
{
	// Nothing is sent: every peer keeps its own component, as offline.
	AUDIENCE_NONE = 0,
	// The peer owning the component's parent (a player controller, or a pawn
	// through its controller).
	AUDIENCE_OWNER = 1,
	// Every peer that has loaded the level.
	AUDIENCE_EVERYONE = 2,
};

World* world_of(const Node* node);

// A networked client holding a mirror: it may read, but only the server changes it.
bool is_mirror(const Node* node, Audience audience);

// The networked server of a component that sends to somebody.
bool is_sending_server(const Node* node, Audience audience);

// Who owns the component - asked of its parent, so a component beside a Pawn
// belongs to whoever controls that pawn.
int get_owner_peer(const Node* node);

// The peers to send changes to: those that asked for the full state (so they
// certainly have the node - a peer can ask while the net driver still counts
// it as loading), are still connected, and are in the audience. Disconnected
// ones are dropped from subscribers.
PackedInt32Array resolve_peers(const Node* node, Audience audience, HashSet<int>& subscribers);

// For an RPC handler: true when the peer that sent it owns the component.
bool sender_is_owner(const Node* node, const char* what);

// Reliable RPCs on channel 0 - the channel nodes are spawned on, so state never
// overtakes the node it is meant for.
void configure_rpcs(Node* node, const PackedStringArray& from_anyone, const PackedStringArray& from_server);

// The path a client loads the same resource from, or empty for one built in
// code or embedded in a scene (which cannot be sent).
String loadable_path(const Ref<Resource>& resource);
}
}

#endif
