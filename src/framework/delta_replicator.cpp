#include "framework/delta_replicator.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "framework/net_driver.h"
#include "framework/world.h"

namespace GFGD
{
namespace
{
const char* KEY_WHOLE = "=";
const char* KEY_SIZE = "a";
const char* KEY_ELEMENTS = "i";
const char* KEY_ENTRIES = "d";
const char* KEY_REMOVED = "-";
const char* KEY_FIELD = "k";
const char* KEY_UPDATED = "u";
const char* KEY_GONE = "r";
const char* KEY_ORDER = "o";

// The reliable state RPCs share channel 0 with node spawning, so they never
// overtake the node they are for. Unreliable patches and acknowledgements get
// a channel of their own.
const int UNRELIABLE_CHANNEL = 1;

Dictionary whole(const Variant& value)
{
	Dictionary patch;
	patch[KEY_WHOLE] = value;
	return patch;
}

// The patch, or the whole value when that encodes no bigger.
Dictionary smaller_of(const Dictionary& patch, const Variant& new_value)
{
	Dictionary replacement = whole(new_value);
	return UtilityFunctions::var_to_bytes(patch).size() < UtilityFunctions::var_to_bytes(replacement).size() ? patch : replacement;
}

Variant deep_copy(const Variant& value)
{
	switch (value.get_type())
	{
		case Variant::ARRAY: return Array(value).duplicate(true);
		case Variant::DICTIONARY: return Dictionary(value).duplicate(true);
		default: return value;
	}
}

bool same(const Variant& a, const Variant& b)
{
	return a.get_type() == b.get_type() && a == b;
}

// key -> element of an Array of Dictionaries, plus the keys in order. False
// when an element is not a Dictionary, has no key, or shares a key.
bool index_by_key(const Array& array, const StringName& key_field, Dictionary& by_key, Array& order)
{
	for (int64_t i = 0; i < array.size(); i++)
	{
		if (array[i].get_type() != Variant::DICTIONARY) { return false; }
		const Dictionary element = array[i];
		if (!element.has(key_field)) { return false; }
		const Variant key = element[key_field];
		if (by_key.has(key)) { return false; }
		by_key[key] = element;
		order.push_back(key);
	}
	return true;
}
}

// --- Patches ---

Variant DeltaReplicator::make_keyed_patch(const Array& old_array, const Array& new_array, const StringName& key_field, bool& ok)
{
	Dictionary old_by_key, new_by_key;
	Array old_order, new_order;
	ok = index_by_key(old_array, key_field, old_by_key, old_order) && index_by_key(new_array, key_field, new_by_key, new_order);
	if (!ok) { return Variant(); }

	Dictionary updated;
	for (int64_t i = 0; i < new_order.size(); i++)
	{
		const Variant& key = new_order[i];
		if (!old_by_key.has(key))
		{
			updated[key] = whole(new_by_key[key]);
			continue;
		}
		const Variant element_patch = make_patch(old_by_key[key], new_by_key[key]);
		if (element_patch.get_type() != Variant::NIL) { updated[key] = element_patch; }
	}

	Array gone;
	Array expected_order;
	for (int64_t i = 0; i < old_order.size(); i++)
	{
		if (new_by_key.has(old_order[i])) { expected_order.push_back(old_order[i]); }
		else { gone.push_back(old_order[i]); }
	}
	for (int64_t i = 0; i < new_order.size(); i++)
	{
		if (!old_by_key.has(new_order[i])) { expected_order.push_back(new_order[i]); }
	}

	Dictionary patch;
	patch[KEY_FIELD] = key_field;
	if (!updated.is_empty()) { patch[KEY_UPDATED] = updated; }
	if (!gone.is_empty()) { patch[KEY_GONE] = gone; }
	// Removals and additions at the end follow from the above; anything else
	// that moved needs the order spelled out.
	if (expected_order != new_order) { patch[KEY_ORDER] = new_order; }
	return smaller_of(patch, new_array);
}

Variant DeltaReplicator::make_patch(const Variant& old_value, const Variant& new_value, const StringName& key_field)
{
	if (same(old_value, new_value)) { return Variant(); }

	if (old_value.get_type() == Variant::ARRAY && new_value.get_type() == Variant::ARRAY)
	{
		const Array old_array = old_value;
		const Array new_array = new_value;
		if (!old_array.is_same_typed(new_array)) { return whole(new_value); }

		if (key_field != StringName())
		{
			bool ok = false;
			const Variant keyed = make_keyed_patch(old_array, new_array, key_field, ok);
			if (ok) { return keyed; }
		}

		Dictionary elements;
		const int64_t shared = MIN(old_array.size(), new_array.size());
		for (int64_t i = 0; i < shared; i++)
		{
			const Variant element_patch = make_patch(old_array[i], new_array[i]);
			if (element_patch.get_type() != Variant::NIL) { elements[i] = element_patch; }
		}
		for (int64_t i = shared; i < new_array.size(); i++)
		{
			elements[i] = whole(new_array[i]);
		}
		Dictionary patch;
		patch[KEY_SIZE] = new_array.size();
		if (!elements.is_empty()) { patch[KEY_ELEMENTS] = elements; }
		return smaller_of(patch, new_value);
	}

	if (old_value.get_type() == Variant::DICTIONARY && new_value.get_type() == Variant::DICTIONARY)
	{
		const Dictionary old_dictionary = old_value;
		const Dictionary new_dictionary = new_value;
		if (!old_dictionary.is_same_typed(new_dictionary)) { return whole(new_value); }

		Dictionary entries;
		const Array new_keys = new_dictionary.keys();
		for (int64_t i = 0; i < new_keys.size(); i++)
		{
			const Variant& key = new_keys[i];
			if (!old_dictionary.has(key))
			{
				entries[key] = whole(new_dictionary[key]);
				continue;
			}
			const Variant entry_patch = make_patch(old_dictionary[key], new_dictionary[key]);
			if (entry_patch.get_type() != Variant::NIL) { entries[key] = entry_patch; }
		}
		Array removed;
		const Array old_keys = old_dictionary.keys();
		for (int64_t i = 0; i < old_keys.size(); i++)
		{
			if (!new_dictionary.has(old_keys[i])) { removed.push_back(old_keys[i]); }
		}
		Dictionary patch;
		if (!entries.is_empty()) { patch[KEY_ENTRIES] = entries; }
		if (!removed.is_empty()) { patch[KEY_REMOVED] = removed; }
		return smaller_of(patch, new_value);
	}

	return whole(new_value);
}

Variant DeltaReplicator::apply_patch(const Variant& old_value, const Variant& patch_variant)
{
	if (patch_variant.get_type() != Variant::DICTIONARY) { return old_value; }
	const Dictionary patch = patch_variant;

	if (patch.has(KEY_WHOLE)) { return patch[KEY_WHOLE]; }

	if (patch.has(KEY_FIELD))
	{
		ERR_FAIL_COND_V_MSG(old_value.get_type() != Variant::ARRAY, old_value, "GFGD: a keyed array patch arrived for a value that is not an Array.");
		const StringName key_field = patch[KEY_FIELD];
		const Array old_array = old_value;
		Dictionary by_key;
		Array old_order;
		ERR_FAIL_COND_V_MSG(!index_by_key(old_array, key_field, by_key, old_order), old_value, "GFGD: a keyed array patch arrived for an array whose elements have no unique key.");

		const Dictionary updated = patch.get(KEY_UPDATED, Dictionary());
		const Array gone = patch.get(KEY_GONE, Array());
		Array order;
		if (patch.has(KEY_ORDER))
		{
			order = patch[KEY_ORDER];
		}
		else
		{
			for (int64_t i = 0; i < old_order.size(); i++)
			{
				if (!gone.has(old_order[i])) { order.push_back(old_order[i]); }
			}
			const Array updated_keys = updated.keys();
			for (int64_t i = 0; i < updated_keys.size(); i++)
			{
				if (!by_key.has(updated_keys[i])) { order.push_back(updated_keys[i]); }
			}
		}

		// Keeps the array's element type.
		Array result = old_array.duplicate(false);
		result.clear();
		for (int64_t i = 0; i < order.size(); i++)
		{
			const Variant& key = order[i];
			const Variant base = by_key.get(key, Variant());
			result.push_back(updated.has(key) ? apply_patch(base, updated[key]) : base);
		}
		return result;
	}

	if (patch.has(KEY_SIZE))
	{
		ERR_FAIL_COND_V_MSG(old_value.get_type() != Variant::ARRAY, old_value, "GFGD: an array patch arrived for a value that is not an Array.");
		Array result = Array(old_value).duplicate(false);
		result.resize(int64_t(patch[KEY_SIZE]));
		const Dictionary elements = patch.get(KEY_ELEMENTS, Dictionary());
		const Array indices = elements.keys();
		for (int64_t i = 0; i < indices.size(); i++)
		{
			const int64_t index = indices[i];
			if (index < 0 || index >= result.size()) { continue; }
			result[index] = apply_patch(result[index], elements[indices[i]]);
		}
		return result;
	}

	if (patch.has(KEY_ENTRIES) || patch.has(KEY_REMOVED))
	{
		ERR_FAIL_COND_V_MSG(old_value.get_type() != Variant::DICTIONARY, old_value, "GFGD: a dictionary patch arrived for a value that is not a Dictionary.");
		Dictionary result = Dictionary(old_value).duplicate(false);
		const Array removed = patch.get(KEY_REMOVED, Array());
		for (int64_t i = 0; i < removed.size(); i++) { result.erase(removed[i]); }
		const Dictionary entries = patch.get(KEY_ENTRIES, Dictionary());
		const Array keys = entries.keys();
		for (int64_t i = 0; i < keys.size(); i++)
		{
			result[keys[i]] = apply_patch(result.get(keys[i], Variant()), entries[keys[i]]);
		}
		return result;
	}

	return old_value;
}

// --- Node ---

Node* DeltaReplicator::get_root_node() const
{
	return get_node_or_null(root_path);
}

bool DeltaReplicator::is_networked_client() const
{
	World* world = is_inside_tree() ? Object::cast_to<World>(get_tree()) : nullptr;
	return world != nullptr && world->is_networked() && !world->has_authority();
}

bool DeltaReplicator::is_networked_server() const
{
	World* world = is_inside_tree() ? Object::cast_to<World>(get_tree()) : nullptr;
	return world != nullptr && world->is_networked() && world->has_authority();
}

bool DeltaReplicator::debug_drop() const
{
	// The same debug setting that drops prediction traffic, applied to what
	// this node sends unreliably - so the re-sending can be watched working.
	if (delivery != DELIVERY_ACKNOWLEDGED) { return false; }
	const double loss = double(ProjectSettings::get_singleton()->get_setting("application/game_framework/debug/network_packet_loss_percent", 0.0)) * 0.01;
	return loss > 0.0 && UtilityFunctions::randf() < loss;
}

Dictionary DeltaReplicator::read_values() const
{
	Dictionary values;
	Node* root = get_root_node();
	if (root == nullptr) { return values; }
	for (int i = 0; i < properties.size(); i++)
	{
		const StringName name = properties[i];
		values[name] = root->get(name);
	}
	return values;
}

void DeltaReplicator::configure_rpcs()
{
	Dictionary reliable;
	reliable["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	reliable["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
	reliable["call_local"] = false;
	reliable["channel"] = 0;
	Dictionary reliable_server = reliable.duplicate();
	reliable_server["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;

	Dictionary patch_mode = reliable_server.duplicate();
	Dictionary ack_mode = reliable.duplicate();
	if (delivery == DELIVERY_ACKNOWLEDGED)
	{
		patch_mode["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;
		patch_mode["channel"] = UNRELIABLE_CHANNEL;
		ack_mode["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;
		ack_mode["channel"] = UNRELIABLE_CHANNEL;
	}

	rpc_config("server_request_full_state", reliable);
	rpc_config("client_receive_full_state", reliable_server);
	rpc_config("client_receive_patch", patch_mode);
	rpc_config("client_receive_patch_reliable", reliable_server);
	rpc_config("server_ack", ack_mode);
}

void DeltaReplicator::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	configure_rpcs();
	if (get_root_node() == nullptr)
	{
		WARN_PRINT(vformat("GFGD: DeltaReplicator %s has no node at root_path \"%s\".", String(get_path()), String(root_path)));
	}

	latest_seq = 0;
	history.clear();
	history[0] = Dictionary(deep_copy(read_values()));
	set_physics_process(true);

	if (is_networked_client())
	{
		rpc_id(World::SERVER_PEER_ID, "server_request_full_state");
	}
}

void DeltaReplicator::_physics_process(double delta)
{
	if (Engine::get_singleton()->is_editor_hint() || !is_networked_server()) { return; }
	for (KeyValue<int, PeerState>& entry : peers) { entry.value.since_send += delta; }
	time_since_check += delta;
	if (time_since_check >= check_interval)
	{
		time_since_check = 0.0;
		bool validate = false;
#ifdef DEBUG_ENABLED
		if (!push_properties.is_empty() && push_validation_interval > 0.0)
		{
			time_since_validation += check_interval > 0.0 ? check_interval : delta;
			if (time_since_validation >= push_validation_interval)
			{
				time_since_validation = 0.0;
				validate = true;
			}
		}
#endif
		record_state(validate);
	}
	send_all(false);
}

bool DeltaReplicator::record_state(bool validate)
{
	Node* root = get_root_node();
	if (root == nullptr) { return false; }

	// The new state starts as the last one: values that did not change stay
	// the very same objects, which keeps both this and later comparisons cheap
	// (an Array or Dictionary compared with itself returns at once).
	const Dictionary last = history[latest_seq];
	Dictionary next = last.duplicate(false);
	bool changed = false;

	for (int i = 0; i < properties.size(); i++)
	{
		const StringName name = properties[i];
		const Variant old_value = last.get(name, Variant());
		Variant new_value = old_value;

		if (!is_push(name) || dirty_properties.has(name))
		{
			const Variant current = root->get(name);
			if (!same(old_value, current)) { new_value = deep_copy(current); }
		}
		else if (dirty_elements.has(name))
		{
			const StringName key_field = keyed_arrays.get(name, StringName());
			const Variant current = root->get(name);
			new_value = key_field != StringName() ? rebuild_keyed(old_value, current, key_field, dirty_elements[name]) : Variant();
			if (new_value.get_type() == Variant::NIL && current.get_type() != Variant::NIL)
			{
				// Not a keyed array after all: compare it whole.
				new_value = same(old_value, current) ? old_value : deep_copy(current);
			}
		}
		else if (validate)
		{
			const Variant current = root->get(name);
			if (!same(old_value, current))
			{
				if (!warned_unmarked.has(name))
				{
					warned_unmarked.insert(name);
					WARN_PRINT(vformat("GFGD: push-based \"%s\" of %s changed without mark_property_dirty / mark_element_dirty; replicating it, but mark it where it changes.", String(name), String(root->get_path())));
				}
				new_value = deep_copy(current);
			}
		}
		else
		{
			comparisons_skipped++;
		}

		if (!same(old_value, new_value))
		{
			next[name] = new_value;
			changed = true;
		}
	}

	dirty_properties.clear();
	dirty_elements.clear();
	if (!changed) { return false; }
	latest_seq++;
	history[latest_seq] = next;
	trim_history();
	return true;
}

Variant DeltaReplicator::rebuild_keyed(const Variant& old_value, const Variant& current, const StringName& key_field, const Dictionary& keys) const
{
	// The current array, in its current order, where every element nobody
	// marked is the previous state's object - not compared, not copied.
	if (current.get_type() != Variant::ARRAY) { return Variant(); }
	const Array current_array = current;
	Dictionary old_by_key;
	Array old_order;
	if (old_value.get_type() != Variant::ARRAY || !index_by_key(old_value, key_field, old_by_key, old_order)) { return Variant(); }

	Array result = Array(old_value).duplicate(false);
	if (!result.is_same_typed(current_array)) { return Variant(); }
	result.clear();
	HashSet<Variant, VariantHasher, VariantComparator> seen;
	for (int64_t i = 0; i < current_array.size(); i++)
	{
		if (current_array[i].get_type() != Variant::DICTIONARY) { return Variant(); }
		const Dictionary element = current_array[i];
		if (!element.has(key_field)) { return Variant(); }
		const Variant key = element[key_field];
		if (seen.has(key)) { return Variant(); }
		seen.insert(key);
		if (keys.has(key) || !old_by_key.has(key)) { result.push_back(element.duplicate(true)); }
		else { result.push_back(old_by_key[key]); }
	}
	return result;
}

void DeltaReplicator::mark_property_dirty(const StringName& property)
{
	dirty_properties.insert(property);
}

void DeltaReplicator::mark_element_dirty(const StringName& property, const Variant& key)
{
	if (!keyed_arrays.has(property))
	{
		dirty_properties.insert(property);
		return;
	}
	if (!dirty_elements.has(property)) { dirty_elements[property] = Dictionary(); }
	Dictionary keys = dirty_elements[property];
	keys[key] = true;
}

Dictionary DeltaReplicator::build_patches(int64_t base, int64_t seq) const
{
	Dictionary patches;
	const Dictionary from = history.get(base);
	const Dictionary to = history.get(seq);
	for (int i = 0; i < properties.size(); i++)
	{
		const StringName name = properties[i];
		const Variant patch = make_patch(from.get(name, Variant()), to.get(name, Variant()), keyed_arrays.get(name, StringName()));
		if (patch.get_type() != Variant::NIL) { patches[name] = patch; }
	}
	return patches;
}

bool DeltaReplicator::peer_may_receive(int peer) const
{
	World* world = Object::cast_to<World>(get_tree());
	NetDriver* driver = world != nullptr ? world->get_net_driver() : nullptr;
	if (driver == nullptr || !driver->get_ready_peers().has(peer)) { return false; }
	if (audience == AUDIENCE_OWNER)
	{
		Node* root = get_root_node();
		return root != nullptr && world->get_net_owner_peer(root) == peer;
	}
	return true;
}

void DeltaReplicator::send_to_peer(int peer, PeerState& state, bool force)
{
	if (state.acked == latest_seq) { return; }
	const bool newer = state.last_sent != latest_seq;
	if (!newer && !force && state.since_send < resend_interval) { return; }
	if (!newer && state.last_reliable) { return; }
	if (!newer) { resends++; }

	if (state.acked < 0 || !history.has(state.acked))
	{
		// Nothing confirmed to build on: the full values, reliably.
		const Dictionary& values = history[latest_seq];
		rpc_id(peer, "client_receive_full_state", latest_seq, values);
		bytes_sent += UtilityFunctions::var_to_bytes(values).size();
		state.last_reliable = true;
	}
	else
	{
		const Dictionary patches = build_patches(state.acked, latest_seq);
		const int64_t size = UtilityFunctions::var_to_bytes(patches).size();
		patches_sent++;
		bytes_sent += size;
		if (delivery == DELIVERY_ACKNOWLEDGED && size > max_unreliable_bytes)
		{
			reliable_patches++;
			rpc_id(peer, "client_receive_patch_reliable", state.acked, latest_seq, patches);
			state.last_reliable = true;
		}
		else
		{
			if (!debug_drop()) { rpc_id(peer, "client_receive_patch", state.acked, latest_seq, patches); }
			state.last_reliable = delivery == DELIVERY_RELIABLE;
		}
	}
	state.last_sent = latest_seq;
	state.since_send = 0.0;
	// Reliable delivery needs no word back: what was sent will arrive, in order.
	if (delivery == DELIVERY_RELIABLE) { state.acked = latest_seq; }
}

void DeltaReplicator::send_all(bool force)
{
	// A client asks for the full state from its _ready, which can be before the
	// net driver counts it as ready (it is still loading). Such a peer is only
	// skipped until it is; only one that disconnected is dropped.
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const PackedInt32Array connected = multiplayer.is_valid() ? multiplayer->get_peers() : PackedInt32Array();
	PackedInt32Array gone;
	for (KeyValue<int, PeerState>& entry : peers)
	{
		if (!connected.has(entry.key))
		{
			gone.push_back(entry.key);
			continue;
		}
		if (!peer_may_receive(entry.key)) { continue; }
		send_to_peer(entry.key, entry.value, force);
	}
	for (int i = 0; i < gone.size(); i++) { peers.erase(gone[i]); }
	if (!gone.is_empty()) { trim_history(); }
}

void DeltaReplicator::trim_history()
{
	// A peer that fell further behind than the history reaches gets the full
	// values instead.
	const int64_t oldest_allowed = latest_seq - max_history;
	int64_t needed = latest_seq;
	for (KeyValue<int, PeerState>& entry : peers)
	{
		if (entry.value.acked >= 0 && entry.value.acked < oldest_allowed) { entry.value.acked = -1; }
		if (entry.value.acked >= 0) { needed = MIN(needed, entry.value.acked); }
	}
	PackedInt64Array old;
	for (const KeyValue<int64_t, Dictionary>& entry : history)
	{
		if (entry.key < needed) { old.push_back(entry.key); }
	}
	for (int i = 0; i < old.size(); i++) { history.erase(old[i]); }
}

bool DeltaReplicator::flush()
{
	if (!is_networked_server()) { return false; }
	const bool changed = record_state();
	send_all(false);
	return changed;
}

PackedInt32Array DeltaReplicator::get_subscribers() const
{
	PackedInt32Array result;
	for (const KeyValue<int, PeerState>& entry : peers) { result.push_back(entry.key); }
	return result;
}

int64_t DeltaReplicator::get_peer_acked_sequence(int peer) const
{
	const PeerState* state = peers.getptr(peer);
	return state != nullptr ? state->acked : -1;
}

// --- RPC: server ---

void DeltaReplicator::server_request_full_state()
{
	if (!is_networked_server()) { return; }
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int sender = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	if (sender <= 0) { return; }
	if (audience == AUDIENCE_OWNER)
	{
		World* world = Object::cast_to<World>(get_tree());
		Node* root = get_root_node();
		if (root == nullptr || world->get_net_owner_peer(root) != sender) { return; }
	}
	record_state();
	peers[sender] = PeerState();
	send_to_peer(sender, peers[sender], true);
}

void DeltaReplicator::server_ack(int64_t seq)
{
	if (!is_networked_server()) { return; }
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int sender = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	PeerState* state = peers.getptr(sender);
	if (state == nullptr || seq <= state->acked || seq > latest_seq || !history.has(seq)) { return; }
	state->acked = seq;
	trim_history();
}

// --- RPC: client ---

void DeltaReplicator::apply_state(int64_t seq, const Dictionary& values)
{
	Node* root = get_root_node();
	if (root == nullptr) { return; }
	const Dictionary previous = client_seq >= 0 ? client_history[client_seq] : read_values();
	for (int i = 0; i < properties.size(); i++)
	{
		const StringName name = properties[i];
		if (!values.has(name)) { continue; }
		const Variant new_value = values[name];
		const Variant old_value = previous.get(name, Variant());
		if (client_seq >= 0 && same(old_value, new_value)) { continue; }
		root->set(name, deep_copy(new_value));
		emit_signal("property_changed", name);
		if (keyed_arrays.has(name)) { emit_element_signals(name, old_value, new_value); }
	}
	client_history[seq] = Dictionary(deep_copy(values));
	client_seq = seq;
	emit_signal("synchronized");
}

void DeltaReplicator::emit_element_signals(const StringName& property, const Variant& old_value, const Variant& new_value)
{
	const StringName key_field = keyed_arrays[property];
	Dictionary old_by_key, new_by_key;
	Array old_order, new_order;
	if (old_value.get_type() == Variant::ARRAY && !index_by_key(old_value, key_field, old_by_key, old_order)) { return; }
	if (new_value.get_type() != Variant::ARRAY || !index_by_key(new_value, key_field, new_by_key, new_order)) { return; }
	for (int64_t i = 0; i < old_order.size(); i++)
	{
		if (!new_by_key.has(old_order[i])) { emit_signal("element_removed", property, old_order[i]); }
	}
	for (int64_t i = 0; i < new_order.size(); i++)
	{
		const Variant& key = new_order[i];
		if (!old_by_key.has(key)) { emit_signal("element_added", property, key, new_by_key[key]); }
		else if (!same(old_by_key[key], new_by_key[key])) { emit_signal("element_changed", property, key, new_by_key[key]); }
	}
}

void DeltaReplicator::client_receive_full_state(int64_t seq, const Dictionary& values)
{
	if (seq > client_seq)
	{
		apply_state(seq, values);
		client_history.clear();
		client_history[seq] = Dictionary(deep_copy(values));
	}
	if (delivery == DELIVERY_ACKNOWLEDGED && !debug_drop())
	{
		rpc_id(World::SERVER_PEER_ID, "server_ack", client_seq);
	}
}

void DeltaReplicator::client_receive_patch(int64_t base, int64_t seq, const Dictionary& patches)
{
	// Before the full state the local values are whatever the scene started
	// with, which no patch is relative to; the full state is on its way.
	if (client_seq < 0) { return; }
	if (seq > client_seq && client_history.has(base))
	{
		Dictionary values = client_history[base].duplicate(false);
		const Array names = patches.keys();
		for (int64_t i = 0; i < names.size(); i++)
		{
			values[names[i]] = apply_patch(values.get(names[i], Variant()), patches[names[i]]);
		}
		apply_state(seq, values);
		// The server only ever builds on a state this peer confirmed, and it
		// confirmed base at the latest - nothing older is needed again.
		PackedInt64Array old;
		for (const KeyValue<int64_t, Dictionary>& entry : client_history)
		{
			if (entry.key < base) { old.push_back(entry.key); }
		}
		for (int i = 0; i < old.size(); i++) { client_history.erase(old[i]); }
	}
	// Also for a duplicate or a stale patch: the earlier acknowledgement may be
	// what got lost.
	if (delivery == DELIVERY_ACKNOWLEDGED && !debug_drop())
	{
		rpc_id(World::SERVER_PEER_ID, "server_ack", client_seq);
	}
}

void DeltaReplicator::_bind_methods()
{
	BIND_ENUM_CONSTANT(AUDIENCE_EVERYONE);
	BIND_ENUM_CONSTANT(AUDIENCE_OWNER);
	BIND_ENUM_CONSTANT(DELIVERY_ACKNOWLEDGED);
	BIND_ENUM_CONSTANT(DELIVERY_RELIABLE);

	ClassDB::bind_method(D_METHOD("flush"), &DeltaReplicator::flush);
	ClassDB::bind_method(D_METHOD("mark_property_dirty", "property"), &DeltaReplicator::mark_property_dirty);
	ClassDB::bind_method(D_METHOD("mark_element_dirty", "property", "key"), &DeltaReplicator::mark_element_dirty);
	ClassDB::bind_method(D_METHOD("get_comparisons_skipped"), &DeltaReplicator::get_comparisons_skipped);
	ClassDB::bind_method(D_METHOD("set_push_properties", "value"), &DeltaReplicator::set_push_properties);
	ClassDB::bind_method(D_METHOD("get_push_properties"), &DeltaReplicator::get_push_properties);
	ClassDB::bind_method(D_METHOD("set_push_validation_interval", "value"), &DeltaReplicator::set_push_validation_interval);
	ClassDB::bind_method(D_METHOD("get_push_validation_interval"), &DeltaReplicator::get_push_validation_interval);
	ClassDB::bind_static_method("DeltaReplicator", D_METHOD("make_patch", "old_value", "new_value", "key_field"), &DeltaReplicator::make_patch, DEFVAL(StringName()));
	ClassDB::bind_static_method("DeltaReplicator", D_METHOD("apply_patch", "old_value", "patch"), &DeltaReplicator::apply_patch);
	ClassDB::bind_method(D_METHOD("has_full_state"), &DeltaReplicator::has_full_state);
	ClassDB::bind_method(D_METHOD("get_state_sequence"), &DeltaReplicator::get_state_sequence);
	ClassDB::bind_method(D_METHOD("get_bytes_sent"), &DeltaReplicator::get_bytes_sent);
	ClassDB::bind_method(D_METHOD("get_patches_sent"), &DeltaReplicator::get_patches_sent);
	ClassDB::bind_method(D_METHOD("get_resends"), &DeltaReplicator::get_resends);
	ClassDB::bind_method(D_METHOD("get_subscribers"), &DeltaReplicator::get_subscribers);
	ClassDB::bind_method(D_METHOD("get_peer_acked_sequence", "peer"), &DeltaReplicator::get_peer_acked_sequence);

	ClassDB::bind_method(D_METHOD("set_root_path", "value"), &DeltaReplicator::set_root_path);
	ClassDB::bind_method(D_METHOD("get_root_path"), &DeltaReplicator::get_root_path);
	ClassDB::bind_method(D_METHOD("set_properties", "value"), &DeltaReplicator::set_properties);
	ClassDB::bind_method(D_METHOD("get_properties"), &DeltaReplicator::get_properties);
	ClassDB::bind_method(D_METHOD("set_keyed_arrays", "value"), &DeltaReplicator::set_keyed_arrays);
	ClassDB::bind_method(D_METHOD("get_keyed_arrays"), &DeltaReplicator::get_keyed_arrays);
	ClassDB::bind_method(D_METHOD("set_audience", "value"), &DeltaReplicator::set_audience);
	ClassDB::bind_method(D_METHOD("get_audience"), &DeltaReplicator::get_audience);
	ClassDB::bind_method(D_METHOD("set_delivery", "value"), &DeltaReplicator::set_delivery);
	ClassDB::bind_method(D_METHOD("get_delivery"), &DeltaReplicator::get_delivery);
	ClassDB::bind_method(D_METHOD("set_check_interval", "value"), &DeltaReplicator::set_check_interval);
	ClassDB::bind_method(D_METHOD("get_check_interval"), &DeltaReplicator::get_check_interval);
	ClassDB::bind_method(D_METHOD("set_resend_interval", "value"), &DeltaReplicator::set_resend_interval);
	ClassDB::bind_method(D_METHOD("get_resend_interval"), &DeltaReplicator::get_resend_interval);
	ClassDB::bind_method(D_METHOD("set_max_history", "value"), &DeltaReplicator::set_max_history);
	ClassDB::bind_method(D_METHOD("get_max_history"), &DeltaReplicator::get_max_history);

	ClassDB::bind_method(D_METHOD("server_request_full_state"), &DeltaReplicator::server_request_full_state);
	ClassDB::bind_method(D_METHOD("server_ack", "seq"), &DeltaReplicator::server_ack);
	ClassDB::bind_method(D_METHOD("client_receive_full_state", "seq", "values"), &DeltaReplicator::client_receive_full_state);
	ClassDB::bind_method(D_METHOD("client_receive_patch", "base", "seq", "patches"), &DeltaReplicator::client_receive_patch);
	ClassDB::bind_method(D_METHOD("client_receive_patch_reliable", "base", "seq", "patches"), &DeltaReplicator::client_receive_patch_reliable);
	ClassDB::bind_method(D_METHOD("get_reliable_patches"), &DeltaReplicator::get_reliable_patches);
	ClassDB::bind_method(D_METHOD("set_max_unreliable_bytes", "value"), &DeltaReplicator::set_max_unreliable_bytes);
	ClassDB::bind_method(D_METHOD("get_max_unreliable_bytes"), &DeltaReplicator::get_max_unreliable_bytes);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "root_path"), "set_root_path", "get_root_path");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "properties"), "set_properties", "get_properties");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "keyed_arrays"), "set_keyed_arrays", "get_keyed_arrays");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "push_properties"), "set_push_properties", "get_push_properties");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "push_validation_interval", PROPERTY_HINT_RANGE, "0,10,0.1,or_greater,suffix:s"), "set_push_validation_interval", "get_push_validation_interval");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "audience", PROPERTY_HINT_ENUM, "Everyone,Owner"), "set_audience", "get_audience");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "delivery", PROPERTY_HINT_ENUM, "Acknowledged,Reliable"), "set_delivery", "get_delivery");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "check_interval", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_check_interval", "get_check_interval");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "resend_interval", PROPERTY_HINT_RANGE, "0.02,5,0.01,or_greater,suffix:s"), "set_resend_interval", "get_resend_interval");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_unreliable_bytes", PROPERTY_HINT_RANGE, "0,65536,1,or_greater,suffix:B"), "set_max_unreliable_bytes", "get_max_unreliable_bytes");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_history", PROPERTY_HINT_RANGE, "2,1024,1,or_greater"), "set_max_history", "get_max_history");

	ADD_SIGNAL(MethodInfo("property_changed", PropertyInfo(Variant::STRING_NAME, "property")));
	ADD_SIGNAL(MethodInfo("element_added", PropertyInfo(Variant::STRING_NAME, "property"), PropertyInfo(Variant::NIL, "key", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT), PropertyInfo(Variant::DICTIONARY, "element")));
	ADD_SIGNAL(MethodInfo("element_removed", PropertyInfo(Variant::STRING_NAME, "property"), PropertyInfo(Variant::NIL, "key", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	ADD_SIGNAL(MethodInfo("element_changed", PropertyInfo(Variant::STRING_NAME, "property"), PropertyInfo(Variant::NIL, "key", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT), PropertyInfo(Variant::DICTIONARY, "element")));
	ADD_SIGNAL(MethodInfo("synchronized"));
}
}
