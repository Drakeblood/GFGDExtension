#ifndef NET_REPLICATION_H
#define NET_REPLICATION_H

#include <godot_cpp/classes/multiplayer_synchronizer.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

namespace GFGD
{
// Property mirroring for the framework's own nodes.
//
// A node calls attach() on itself from _ready, on every peer rather than only
// on the server: a MultiplayerSynchronizer only pairs up with its counterpart
// when both sides sit at the same path, and _ready is the one callback that is
// guaranteed to run identically wherever the node was built.
namespace Replication
{
// Adds a MultiplayerSynchronizer under owner, configured to mirror the listed
// properties of target. Does nothing when the list is empty.
//
// owner and target differ when a node speaks for its parent: a node may not add
// a child to an ancestor while that ancestor is still starting its children up,
// so the synchronizer is parented here and pointed there.
//
// authority_peer is who sends: everything the framework owns is sent by the
// server, so this is the server's peer id in every current caller.
MultiplayerSynchronizer* attach(Node* owner, Node* target, const PackedStringArray& properties, int authority_peer, const String& synchronizer_name);

// Drops duplicates and names that are not properties of node (which would
// otherwise be a silent no-op inside the synchronizer). Callers merge their own
// built-in list with whatever a script returned before passing it here.
PackedStringArray validate_properties(Node* node, const PackedStringArray& properties);
}
}

#endif
