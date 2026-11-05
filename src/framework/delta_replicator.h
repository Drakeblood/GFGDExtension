#ifndef GFGD_DELTA_REPLICATOR_H
#define GFGD_DELTA_REPLICATOR_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

namespace GFGD
{
// Mirrors properties of a node from the server to clients, sending only what
// changed - down to the element: a changed field of one order in an array of
// orders sends that field, not the array.
//
// For large or structured state (lists, dictionaries, reports). A plain
// property synchronizer sends every value whole on every update, and all of a
// node's values have to fit into one packet; this sends nothing while nothing
// changes, and a change costs about the size of the change.
//
// How it works. Every check_interval the server compares each property with
// the last numbered state (deeply, so arrays and dictionaries edited in place
// are noticed) and, when something differs, records a new state. Each client
// is sent the patch from the state it last CONFIRMED to the newest one:
// - DELIVERY_ACKNOWLEDGED (default): patches go unreliably and the client
//   acknowledges every state it applies. A lost patch is never sent again as
//   such; until the acknowledgement comes, the server re-sends the patch from
//   the confirmed state to whatever is newest then, so a client that missed
//   something catches up in one step, and a slow link never queues stale data.
// - DELIVERY_RELIABLE: patches go over a reliable, ordered RPC, each from the
//   previous state; nothing is re-sent by this node.
// A client asks for the full values when its replicator is ready (sent
// reliably), so a peer joining late starts from the current state.
//
// Push-based properties (push_properties) are never compared on their own:
// the game marks them after changing them - mark_property_dirty(name), or for
// a keyed array mark_element_dirty(name, key) - and only what was marked is
// looked at. The rest of the array is not even compared: unchanged elements
// are shared with the previous state, so the check costs as much as the
// marked elements. In debug builds, every push_validation_interval the server
// compares them all anyway and warns about a change nobody marked (and
// replicates it).
//
// keyed_arrays maps an Array property to a key field of its Dictionary
// elements ({"orders": "id"}): its elements are then matched by key instead of
// by index - removing one from the middle sends just its key - and clients get
// element_added / element_removed / element_changed for it.
//
// Patch format (also what make_patch returns):
//   {"=": value}                               the whole value
//   {"a": size, "i": {index: patch}}           an Array: new size, changed elements
//   {"k": field, "u": {key: patch}, "r": [keys], "o": [keys]}
//                                              a keyed Array: changed/added elements,
//                                              removed keys, the key order if it moved
//   {"d": {key: patch}, "-": [keys]}           a Dictionary: changed and removed keys
// Packed arrays, differently typed containers, and anything whose patch would
// not be smaller are sent whole.
class DeltaReplicator : public Node
{
	GDCLASS(DeltaReplicator, Node)

public:
	enum Audience
	{
		AUDIENCE_EVERYONE = 0,
		AUDIENCE_OWNER = 1,
	};

	enum Delivery
	{
		DELIVERY_ACKNOWLEDGED = 0,
		DELIVERY_RELIABLE = 1,
	};

private:
	struct PeerState
	{
		// The newest state this peer confirmed; -1 = none, it needs everything.
		int64_t acked = -1;
		int64_t last_sent = -1;
		double since_send = 0.0;
		// The last send went reliably: it will arrive, so it is not repeated.
		bool last_reliable = false;
	};

	NodePath root_path = NodePath("..");
	PackedStringArray properties;
	Dictionary keyed_arrays;
	PackedStringArray push_properties;
	double push_validation_interval = 1.0;
	Audience audience = AUDIENCE_EVERYONE;
	Delivery delivery = DELIVERY_ACKNOWLEDGED;
	double check_interval = 0.1;
	double resend_interval = 0.25;
	int max_history = 64;
	// Bigger patches are sent reliably even in DELIVERY_ACKNOWLEDGED: an
	// unreliable packet over the MTU is split, and losing any piece loses it.
	int max_unreliable_bytes = 1200;

	// Server.
	int64_t latest_seq = 0;
	HashMap<int64_t, Dictionary> history;
	HashMap<int, PeerState> peers;
	double time_since_check = 0.0;
	double time_since_validation = 0.0;
	// Push: properties marked whole, and marked elements of keyed arrays
	// (property -> {key: true}).
	HashSet<StringName> dirty_properties;
	Dictionary dirty_elements;
	HashSet<StringName> warned_unmarked;
	int64_t comparisons_skipped = 0;
	int64_t bytes_sent = 0;
	int64_t patches_sent = 0;
	int64_t resends = 0;
	int64_t reliable_patches = 0;

	// Client.
	int64_t client_seq = -1;
	HashMap<int64_t, Dictionary> client_history;

	Node* get_root_node() const;
	bool is_networked_client() const;
	bool is_networked_server() const;
	bool peer_may_receive(int peer) const;
	bool debug_drop() const;
	Dictionary read_values() const;
	bool is_push(const StringName& name) const { return push_properties.has(name); }
	bool record_state(bool validate = false);
	Variant rebuild_keyed(const Variant& old_value, const Variant& current, const StringName& key_field, const Dictionary& keys) const;
	Dictionary build_patches(int64_t base, int64_t seq) const;
	void send_to_peer(int peer, PeerState& state, bool force);
	void send_all(bool force);
	void trim_history();
	void configure_rpcs();
	void apply_state(int64_t seq, const Dictionary& values);
	void emit_element_signals(const StringName& property, const Variant& old_value, const Variant& new_value);

	static Variant make_keyed_patch(const Array& old_array, const Array& new_array, const StringName& key_field, bool& ok);

public:
	virtual void _ready() override;
	virtual void _physics_process(double delta) override;

	// Server: compares now and sends whatever changed, without waiting for the
	// next check. Returns true when there was a change.
	bool flush();

	// Server, push-based properties: this one changed - compare it whole at the
	// next check. Harmless on a polled property.
	void mark_property_dirty(const StringName& property);
	// Server, push-based keyed array: the element with this key changed, was
	// added or was removed. Only marked elements are compared. On a property
	// that is not a keyed array this marks the whole property.
	void mark_element_dirty(const StringName& property, const Variant& key);

	// The patch turning old_value into new_value, or null when they are equal.
	// With key_field, an Array of Dictionaries is matched by that field.
	static Variant make_patch(const Variant& old_value, const Variant& new_value, const StringName& key_field = StringName());
	// old_value with the patch applied. Containers are copied, never changed in place.
	static Variant apply_patch(const Variant& old_value, const Variant& patch);

	bool has_full_state() const { return client_seq >= 0; }
	int64_t get_state_sequence() const { return is_networked_client() ? client_seq : latest_seq; }
	int64_t get_bytes_sent() const { return bytes_sent; }
	int64_t get_patches_sent() const { return patches_sent; }
	int64_t get_resends() const { return resends; }
	// Server: patches sent reliably for being over max_unreliable_bytes.
	int64_t get_reliable_patches() const { return reliable_patches; }
	// Server: push-based properties left uncompared because nothing marked them.
	int64_t get_comparisons_skipped() const { return comparisons_skipped; }
	PackedInt32Array get_subscribers() const;
	// Server: the newest state the peer confirmed (-1 = none yet).
	int64_t get_peer_acked_sequence(int peer) const;

	void set_root_path(const NodePath& value) { root_path = value; }
	NodePath get_root_path() const { return root_path; }
	void set_properties(const PackedStringArray& value) { properties = value; }
	PackedStringArray get_properties() const { return properties; }
	void set_keyed_arrays(const Dictionary& value) { keyed_arrays = value; }
	Dictionary get_keyed_arrays() const { return keyed_arrays; }
	void set_push_properties(const PackedStringArray& value) { push_properties = value; }
	PackedStringArray get_push_properties() const { return push_properties; }
	void set_push_validation_interval(double value) { push_validation_interval = MAX(value, 0.0); }
	double get_push_validation_interval() const { return push_validation_interval; }
	void set_audience(Audience value) { audience = value; }
	Audience get_audience() const { return audience; }
	void set_delivery(Delivery value) { delivery = value; }
	Delivery get_delivery() const { return delivery; }
	void set_check_interval(double value) { check_interval = MAX(value, 0.0); }
	double get_check_interval() const { return check_interval; }
	void set_resend_interval(double value) { resend_interval = MAX(value, 0.02); }
	double get_resend_interval() const { return resend_interval; }
	void set_max_unreliable_bytes(int value) { max_unreliable_bytes = MAX(value, 0); }
	int get_max_unreliable_bytes() const { return max_unreliable_bytes; }
	void set_max_history(int value) { max_history = MAX(value, 2); }
	int get_max_history() const { return max_history; }

	// --- RPC endpoints ---
	void server_request_full_state();
	void server_ack(int64_t seq);
	void client_receive_full_state(int64_t seq, const Dictionary& values);
	void client_receive_patch(int64_t base, int64_t seq, const Dictionary& patches);
	// The same, for a patch too big to go unreliably.
	void client_receive_patch_reliable(int64_t base, int64_t seq, const Dictionary& patches) { client_receive_patch(base, seq, patches); }

protected:
	static void _bind_methods();
};
}

VARIANT_ENUM_CAST(GFGD::DeltaReplicator::Audience);
VARIANT_ENUM_CAST(GFGD::DeltaReplicator::Delivery);

#endif
