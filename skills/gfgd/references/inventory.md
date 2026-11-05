# Inventory, quick bar and equipment

Three independent components plus data resources. Use what the game needs.

## Where the nodes go

```
PlayerController (scene)          ← outlives the pawn: the backpack survives a respawn
 ├─ InventoryManagerComponent     replication_mode = OWNER (default)
 └─ QuickBarComponent             num_slots = 5; finds the inventory beside it
Pawn root
 ├─ Pawn
 ├─ AbilitySystemComponent        abilities / effects / tags from equipment land here
 └─ EquipmentManagerComponent     replication_mode = EVERYONE (default)
```

A quick bar under a controller equips on `controller.get_pawn_root()`, and re-equips when the
controller possesses a new pawn (`pawn_changed`). Under a pawn root it equips on the manager beside it.
A chest: an `InventoryManagerComponent` with `replication_mode = REPLICATION_EVERYONE` in the level.

## Data

```gdscript
# res://items/id_axe.tres  — InventoryItemDefinition
display_name = "Axe"; max_stack_size = 1
fragments = [InventoryFragmentEquippableItem(equipment_definition = ed_axe.tres),
             InventoryFragmentQuickBarIcon(icon = axe.svg)]

# res://items/ed_axe.tres  — EquipmentDefinition
abilities_to_grant = [ga_chop.tres]          # granted on the server, source object = the item
granted_tags = ["State.Equipped.Axe"]         # loose tags while it is on
actors_to_spawn = [EquipmentActorToSpawn(scene = axe.gltf, attach_bone = &"handslot.r",
                                         attach_transform = ...)]
```

- Definitions and equipment definitions that reach clients **must be saved `.tres` files** — they
  replicate as resource paths. A definition built in code works offline only (a warning says so).
- `attach_bone` searches every `Skeleton3D` at or below `attach_to` (default: the pawn root), so a
  skeleton built at runtime needs no fixed path. The scene must be purely visual (not networked):
  every peer instantiates its own copy.
- Game fragments: `extends InventoryItemFragment`, export data, optionally `_on_instance_created`.
  Find them with `item.find_fragment_by_class(preload("my_fragment.gd"))` or by `class_name`.

## Server API

```gdscript
inventory.add_item_definition(def, 1)   # always a new entry (own state); null when full
inventory.add_items(def, 5)             # tops up stacks first; returns how many fitted
inventory.consume_items(def, 2)         # all or nothing
inventory.set_stack_count(item, n)      # 0 removes
other_inventory.add_item_instance(item) # moves it, with its stats and data
quick_bar.set_active_slot_index(0)      # equips; -1 = nothing (if allow_no_active_slot)
equipment.equip_item(ed_armor, item)    # direct, no quick bar
```

On a client's mirror every mutating call errors — except `QuickBarComponent.set_active_slot_index`,
`cycle_active_slot_forward/backward` and `swap_slots`, which become requests to the server (owner only).
So a slot key can call `quick_bar.set_active_slot_index(i)` wherever the pawn is locally controlled;
do **not** also handle it from the host's replayed input, or a remote press runs twice.

## Reading (HUD)

- The local player's quick bar: `world.get_first_player_controller()` → its `QuickBarComponent` child.
- `get_slots()` → Array of `InventoryItemInstance` / null. Slots store item ids; on a client an id can
  arrive before its item — the bar emits `slots_changed` again when the inventory catches up, so
  redraw on `slots_changed`, `active_slot_changed` and the inventory's `changed`.
- Count: `inventory.get_stack_count(item)` or a stat tag (`InventoryFragmentQuickBarIcon.count_stat_tag`).
- `EquipmentManagerComponent.get_equipment_instances()` works on every peer (EVERYONE) — e.g. to hide
  the tool while the hands carry something: `instance.set_spawned_visible(false)` (local only).

## Gotchas

- `item_id` is unique across all inventories of the process and stays with the instance when it moves.
- `get_total_item_count_by_definition` counts **units**; `get_entry_count` counts entries.
- An item removed from the inventory leaves its quick bar slot and comes off the pawn automatically.
- Abilities granted by equipment are cleared when it comes off — do not keep them in
  `AbilitySystemComponent.startup_abilities` too, or they stay after unequip.
- `EquipmentManagerComponent` takes everything off in `_exit_tree` (a despawned pawn).
