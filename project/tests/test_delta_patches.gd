extends SceneTree

# DeltaReplicator's patches on one machine: make_patch(old, new) applied to old
# with apply_patch must give new back - for hand-picked cases and for a few
# thousand random nested arrays and dictionaries - and a small change to a big
# value must make a small patch.
#
# The network side (full state for a joining peer, patches after it) is
# exercised by any game using _get_delta_replicated_properties.
#
# Run with:
#   godot --headless --path project --script res://tests/test_delta_patches.gd

var failures: Array[String] = []
var rng := RandomNumberGenerator.new()


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _round_trip(name: String, old_value: Variant, new_value: Variant, key := &"") -> Variant:
	var old_copy: Variant = _copy(old_value)
	var patch: Variant = DeltaReplicator.make_patch(old_value, new_value, key)
	var result: Variant = DeltaReplicator.apply_patch(old_value, patch) if patch != null else old_value
	var same: bool = typeof(result) == typeof(new_value) and result == new_value
	var untouched: bool = typeof(old_value) == typeof(old_copy) and old_value == old_copy
	_check(name, "" if same and untouched else "got %s, expected %s%s" % [result, new_value,
		"" if untouched else " (and the old value was changed in place)"])
	return patch


func _copy(value: Variant) -> Variant:
	if value is Array or value is Dictionary:
		return value.duplicate(true)
	return value


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	print("=== cases ===")
	_check("equal values make no patch", "" if DeltaReplicator.make_patch([1, {"a": 2}], [1, {"a": 2}]) == null else "patched")
	_round_trip("scalar", 3, 4)
	_round_trip("type change", 3, "three")
	_round_trip("array grows", [1, 2], [1, 2, 3, 4])
	_round_trip("array shrinks", [1, 2, 3, 4], [1, 2])
	_round_trip("array emptied", [1, 2], [])
	_round_trip("element changed", [1, 2, 3], [1, 5, 3])
	_round_trip("removal near the front", [1, 2, 3, 4, 5], [2, 3, 4, 5])
	_round_trip("dictionary key added", {"a": 1}, {"a": 1, "b": 2})
	_round_trip("dictionary key removed", {"a": 1, "b": 2}, {"a": 1})
	_round_trip("dictionary emptied", {"a": 1}, {})
	_round_trip("nested field", [{"id": 1, "delivered": 0}, {"id": 2, "delivered": 0}],
		[{"id": 1, "delivered": 0}, {"id": 2, "delivered": 1}])
	_round_trip("typed array replaced by untyped", [1, 2] as Array[int], [1, 2, 3])
	_round_trip("array to dictionary", [1], {"a": 1})
	_round_trip("packed array", PackedStringArray(["a"]), PackedStringArray(["a", "b"]))

	print("=== size ===")
	var orders: Array = []
	for i in 40:
		orders.append({"id": i, "client": "Client %d" % i, "species": "Wood.Species.Pine", "length": 2.5,
			"count": 4, "delivered": 0, "reward_each": 48, "destination": "yard"})
	var changed := orders.duplicate(true)
	changed[17]["delivered"] = 3
	var patch: Variant = _round_trip("one field in 40 orders", orders, changed)
	var patch_bytes := var_to_bytes(patch).size()
	var whole_bytes := var_to_bytes(changed).size()
	_check("its patch is small", "" if patch_bytes * 20 < whole_bytes else "%d bytes vs %d whole" % [patch_bytes, whole_bytes])
	print("      patch %d B, whole value %d B" % [patch_bytes, whole_bytes])
	changed.remove_at(0)
	patch = _round_trip("first of 40 orders removed", orders, changed)
	_check("is never bigger than the whole value", "" if var_to_bytes(patch).size() <= var_to_bytes({"=": changed}).size() else "bigger")

	print("=== keyed arrays ===")
	var keyed_patch: Variant = _round_trip("keyed: first of 40 removed", orders, changed, &"id")
	var keyed_bytes := var_to_bytes(keyed_patch).size()
	var indexed_bytes := var_to_bytes(DeltaReplicator.make_patch(orders, changed)).size()
	_check("sends just the removed key", "" if keyed_bytes < 200 and keyed_bytes * 20 < indexed_bytes else "%d bytes" % keyed_bytes)
	print("      keyed %d B, by index %d B" % [keyed_bytes, indexed_bytes])
	_check("as a keyed patch", "" if keyed_patch is Dictionary and keyed_patch.has("k") and keyed_patch.has("r") and not keyed_patch.has("o") else str(keyed_patch))
	var moved := orders.duplicate(true)
	var last: Dictionary = moved.pop_back()
	moved.insert(0, last)
	keyed_patch = _round_trip("keyed: one moved to the front", orders, moved, &"id")
	_check("carries the order", "" if keyed_patch is Dictionary and keyed_patch.has("o") else str(keyed_patch))
	var edited := orders.duplicate(true)
	edited.remove_at(5)
	edited[10]["delivered"] = 2
	edited.append({"id": 99, "client": "New", "delivered": 0})
	_round_trip("keyed: remove + edit + append", orders, edited, &"id")
	_round_trip("keyed: a duplicate key falls back to indices", [{"id": 1}, {"id": 2}], [{"id": 1}, {"id": 1}], &"id")
	_round_trip("keyed: a non-dictionary element falls back", [{"id": 1}, 5], [{"id": 1}, 6], &"id")
	_round_trip("keyed: typed array keeps its type", [{"id": 1}] as Array[Dictionary], [{"id": 2}, {"id": 1}] as Array[Dictionary], &"id")
	var typed_result: Variant = DeltaReplicator.apply_patch([{"id": 1}] as Array[Dictionary],
		DeltaReplicator.make_patch([{"id": 1}] as Array[Dictionary], [{"id": 2, "x": 1}, {"id": 1}] as Array[Dictionary], &"id"))
	_check("keyed: the result is still Array[Dictionary]", "" if (typed_result as Array).is_typed() else "untyped")

	print("=== random ===")
	rng.seed = 1234
	var bad := 0
	for i in 3000:
		var a: Variant = _random_value(3)
		var b: Variant = _mutate(_copy(a), 3)
		var p: Variant = DeltaReplicator.make_patch(a, b)
		var r: Variant = DeltaReplicator.apply_patch(a, p) if p != null else a
		if typeof(r) != typeof(b) or r != b:
			bad += 1
			if bad <= 3:
				print("      mismatch: %s -> %s, patch %s gave %s" % [a, b, p, r])
	_check("3000 random nested changes round-trip", "" if bad == 0 else "%d mismatched" % bad)
	bad = 0
	for i in 2000:
		var a := _random_keyed()
		var b := _mutate_keyed(a.duplicate(true))
		var p: Variant = DeltaReplicator.make_patch(a, b, &"id")
		var r: Variant = DeltaReplicator.apply_patch(a, p) if p != null else a
		if typeof(r) != typeof(b) or r != b:
			bad += 1
			if bad <= 3:
				print("      keyed mismatch: %s -> %s, patch %s gave %s" % [a, b, p, r])
	_check("2000 random keyed-array changes round-trip", "" if bad == 0 else "%d mismatched" % bad)

	print("")
	if failures.is_empty():
		print("ALL PASSED")
		quit(0)
	else:
		print("%d FAILED" % failures.size())
		quit(1)


func _random_value(depth: int) -> Variant:
	var kind := rng.randi_range(0, 4 if depth > 0 else 2)
	match kind:
		0: return rng.randi_range(0, 5)
		1: return "s%d" % rng.randi_range(0, 3)
		2: return rng.randf_range(0.0, 1.0) > 0.5
		3:
			var array: Array = []
			for i in rng.randi_range(0, 5):
				array.append(_random_value(depth - 1))
			return array
		_:
			var dictionary := {}
			for i in rng.randi_range(0, 5):
				dictionary["k%d" % rng.randi_range(0, 6)] = _random_value(depth - 1)
			return dictionary


func _mutate(value: Variant, depth: int) -> Variant:
	if rng.randf() < 0.15 or depth == 0:
		return _random_value(depth)
	if value is Array:
		var array: Array = value
		match rng.randi_range(0, 3):
			0: array.append(_random_value(depth - 1))
			1:
				if not array.is_empty():
					array.remove_at(rng.randi_range(0, array.size() - 1))
			_:
				if not array.is_empty():
					var i := rng.randi_range(0, array.size() - 1)
					array[i] = _mutate(array[i], depth - 1)
		return array
	if value is Dictionary:
		var dictionary: Dictionary = value
		match rng.randi_range(0, 3):
			0: dictionary["k%d" % rng.randi_range(0, 9)] = _random_value(depth - 1)
			1:
				if not dictionary.is_empty():
					dictionary.erase(dictionary.keys()[rng.randi_range(0, dictionary.size() - 1)])
			_:
				if not dictionary.is_empty():
					var key: Variant = dictionary.keys()[rng.randi_range(0, dictionary.size() - 1)]
					dictionary[key] = _mutate(dictionary[key], depth - 1)
		return dictionary
	return _random_value(depth)


var _next_id := 0


func _random_keyed() -> Array:
	var array: Array = []
	for i in rng.randi_range(0, 8):
		_next_id += 1
		array.append({"id": _next_id, "v": _random_value(1)})
	return array


func _mutate_keyed(array: Array) -> Array:
	for step in rng.randi_range(1, 4):
		match rng.randi_range(0, 4):
			0:
				_next_id += 1
				array.insert(rng.randi_range(0, array.size()), {"id": _next_id, "v": _random_value(1)})
			1:
				if not array.is_empty():
					array.remove_at(rng.randi_range(0, array.size() - 1))
			2:
				if array.size() > 1:
					var element: Variant = array.pop_at(rng.randi_range(0, array.size() - 1))
					array.insert(rng.randi_range(0, array.size()), element)
			_:
				if not array.is_empty():
					array[rng.randi_range(0, array.size() - 1)]["v"] = _random_value(1)
	return array
