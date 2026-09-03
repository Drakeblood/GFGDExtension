#include "framework/gameplay_message_router.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace GFGD
{
GameplayMessageRouter* GameplayMessageRouter::instance = nullptr;

GameplayMessageRouter* GameplayMessageRouter::get_singleton()
{
	if (instance == nullptr)
	{
		instance = memnew(GameplayMessageRouter());
	}
	return instance;
}

void GameplayMessageRouter::destroy_singleton()
{
	if (instance != nullptr)
	{
		memdelete(instance);
		instance = nullptr;
	}
}

GameplayMessageRouter::GameplayMessageRouter()
{
	next_handle = 1;
}

GameplayMessageRouter::~GameplayMessageRouter()
{

}

int64_t GameplayMessageRouter::register_listener(const StringName& channel, const Callable& callback, MatchType match)
{
	if (channel.is_empty())
	{
		ERR_PRINT("GFGD: GameplayMessageRouter.register_listener needs a channel name.");
		return 0;
	}

	if (!callback.is_valid())
	{
		ERR_PRINT(vformat("GFGD: GameplayMessageRouter.register_listener got an invalid callable for channel '%s'.", String(channel)));
		return 0;
	}

	Listener listener;
	listener.handle = next_handle++;
	listener.callback = callback;
	listener.match = match;

	listeners[channel].push_back(listener);
	handle_channels[listener.handle] = channel;

	return listener.handle;
}

void GameplayMessageRouter::unregister_listener(int64_t handle)
{
	HashMap<int64_t, StringName>::Iterator handle_entry = handle_channels.find(handle);
	if (handle_entry == handle_channels.end()) { return; }

	const StringName channel = handle_entry->value;
	handle_channels.remove(handle_entry);

	HashMap<StringName, Vector<Listener>>::Iterator bucket = listeners.find(channel);
	if (bucket == listeners.end()) { return; }

	Vector<Listener>& channel_listeners = bucket->value;
	for (int i = 0; i < channel_listeners.size(); i++)
	{
		if (channel_listeners[i].handle == handle)
		{
			channel_listeners.remove_at(i);
			break;
		}
	}

	if (channel_listeners.is_empty()) { listeners.remove(bucket); }
}

void GameplayMessageRouter::broadcast(const StringName& channel, const Variant& payload)
{
	if (channel.is_empty()) { return; }

	const Vector<StringName> ancestors = channel_ancestors(channel);

	// Matching callables are collected before any of them runs: a listener is
	// free to broadcast again, or to unregister itself, without invalidating the
	// iteration that is calling it.
	Vector<Callable> pending;

	for (int a = 0; a < ancestors.size(); a++)
	{
		const StringName& bucket_name = ancestors[a];
		const bool is_exact = bucket_name == channel;

		HashMap<StringName, Vector<Listener>>::Iterator bucket = listeners.find(bucket_name);
		if (bucket == listeners.end()) { continue; }

		Vector<Listener>& channel_listeners = bucket->value;

		for (int i = channel_listeners.size() - 1; i >= 0; i--)
		{
			const Listener& listener = channel_listeners[i];

			// The listener's object was freed without unregistering. Dropping it
			// here is what keeps "connect and forget" honest for nodes that come
			// and go with a level.
			if (!listener.callback.is_valid())
			{
				handle_channels.erase(listener.handle);
				channel_listeners.remove_at(i);
				continue;
			}

			if (is_exact || listener.match == MATCH_PARTIAL)
			{
				pending.push_back(listener.callback);
			}
		}

		if (channel_listeners.is_empty()) { listeners.remove(bucket); }
	}

	for (int i = 0; i < pending.size(); i++)
	{
		pending[i].call(channel, payload);
	}
}

bool GameplayMessageRouter::has_listeners(const StringName& channel) const
{
	const Vector<StringName> ancestors = channel_ancestors(channel);

	for (int a = 0; a < ancestors.size(); a++)
	{
		HashMap<StringName, Vector<Listener>>::ConstIterator bucket = listeners.find(ancestors[a]);
		if (bucket == listeners.end()) { continue; }

		const bool is_exact = ancestors[a] == channel;
		const Vector<Listener>& channel_listeners = bucket->value;

		for (int i = 0; i < channel_listeners.size(); i++)
		{
			if (!channel_listeners[i].callback.is_valid()) { continue; }
			if (is_exact || channel_listeners[i].match == MATCH_PARTIAL) { return true; }
		}
	}

	return false;
}

void GameplayMessageRouter::clear()
{
	listeners.clear();
	handle_channels.clear();
}

Vector<StringName> GameplayMessageRouter::channel_ancestors(const StringName& channel)
{
	Vector<StringName> ancestors;

	const String full_name = String(channel);
	int search_from = 0;

	while (true)
	{
		const int dot = full_name.find(".", search_from);
		if (dot == -1) { break; }

		ancestors.push_back(StringName(full_name.substr(0, dot)));
		search_from = dot + 1;
	}

	ancestors.push_back(channel);
	return ancestors;
}

void GameplayMessageRouter::_bind_methods()
{
	ClassDB::bind_static_method("GameplayMessageRouter", D_METHOD("get_singleton"), &GameplayMessageRouter::get_singleton);

	ClassDB::bind_method(D_METHOD("broadcast", "channel", "payload"), &GameplayMessageRouter::broadcast, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("register_listener", "channel", "callback", "match"), &GameplayMessageRouter::register_listener, DEFVAL(MATCH_EXACT));
	ClassDB::bind_method(D_METHOD("unregister_listener", "handle"), &GameplayMessageRouter::unregister_listener);
	ClassDB::bind_method(D_METHOD("has_listeners", "channel"), &GameplayMessageRouter::has_listeners);
	ClassDB::bind_method(D_METHOD("clear"), &GameplayMessageRouter::clear);

	BIND_ENUM_CONSTANT(MATCH_EXACT);
	BIND_ENUM_CONSTANT(MATCH_PARTIAL);
}
}
