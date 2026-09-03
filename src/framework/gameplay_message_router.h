#ifndef GAMEPLAY_MESSAGE_ROUTER_H
#define GAMEPLAY_MESSAGE_ROUTER_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/callable.hpp>

using namespace godot;

namespace GFGD
{
// Fire-and-forget message bus keyed by gameplay tag name.
//
// It exists for the announcements that have no natural owner: a hit landed, a
// wave started, the run ended. Wiring those as signals means every listener has
// to reach the emitter first, which for UI built before the level exists is a
// lifetime problem rather than a design one. A channel name is reachable from
// anywhere and costs nothing to hold.
//
// Channels are tag names, so the hierarchy comes for free: a listener registered
// on "Fx" with MATCH_PARTIAL hears "Fx.Impact" and "Fx.Text". The names are not
// resolved through GameplayTagsManager - broadcasting must not register a tag as
// a side effect - but declaring them in a GameplayTagTable anyway documents the
// channels a project uses.
//
// This is a bus, not a contract: nothing guarantees a listener exists, and
// nothing is delivered to a listener registered after the fact.
class GameplayMessageRouter : public Object
{
	GDCLASS(GameplayMessageRouter, Object)

public:
	enum MatchType
	{
		// Only messages broadcast on exactly this channel.
		MATCH_EXACT = 0,
		// This channel and everything below it: "Fx" hears "Fx.Impact".
		MATCH_PARTIAL = 1,
	};

private:
	struct Listener
	{
		int64_t handle = 0;
		Callable callback;
		MatchType match = MATCH_EXACT;
	};

	static GameplayMessageRouter* instance;

	HashMap<StringName, Vector<Listener>> listeners;

	// Handle to the channel it was registered on, so unregister_listener() does
	// not have to scan every bucket.
	HashMap<int64_t, StringName> handle_channels;

	int64_t next_handle;

public:
	static GameplayMessageRouter* get_singleton();
	static void destroy_singleton();

	GameplayMessageRouter();
	~GameplayMessageRouter();

	// Calls every matching listener as callback(channel, payload). The channel is
	// passed because a MATCH_PARTIAL listener cannot otherwise tell what it heard.
	void broadcast(const StringName& channel, const Variant& payload);

	// Returns a handle for unregister_listener(), or 0 if the callable is invalid.
	// A listener whose object is freed is dropped on the next broadcast, so
	// unregistering is good manners rather than a requirement.
	int64_t register_listener(const StringName& channel, const Callable& callback, MatchType match = MATCH_EXACT);

	// A no-op for a handle that was never registered or is already gone.
	void unregister_listener(int64_t handle);

	bool has_listeners(const StringName& channel) const;

	void clear();

protected:
	static void _bind_methods();

private:
	// "A.B.C" yields A, A.B, A.B.C - the full channel last, so exact listeners
	// are collected in the same pass.
	static Vector<StringName> channel_ancestors(const StringName& channel);
};
}

VARIANT_ENUM_CAST(GFGD::GameplayMessageRouter::MatchType);

#endif
