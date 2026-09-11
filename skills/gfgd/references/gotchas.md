# Gotchas — the contracts the API does not tell you

Signatures are introspectable (`addons/gfgdextension/doc_classes/<Class>.xml`, or
`ClassDB.class_get_method_list()`). These are the contracts that are not, and each one is a
mistake that is easy to make. **Nearly every entry here fails silently or with only a warning.**

## The overarching rule

**Nothing crashes.** Missing settings, a missing `Pawn` node, an unloadable tag table — all warn
and fall back. A clean Output panel does not mean the project is configured; it means nothing threw.
When something "does nothing", the first move is always to read the warnings, not to read the code.

## Language and API shape

- **C# cannot inherit GDExtension classes.** `class MyGameMode : GameModeBase` will not work — this
  is a Godot limitation, not a GFGD one. From C#, use the framework compositionally: place the
  nodes, author the resources, call the methods, connect to the signals. Anything requiring an
  override (`_init_game`, `_activate_ability`, `_setup_input_component`, ...) has to be GDScript.
- **Virtual hooks are overrides, not signals.** Methods starting with `_` (`_init_game`,
  `_can_activate_ability`, `_pre_attribute_change`) are overridden in a subclass. Do not try to
  `connect()` to them.
- **A `Controller` subclass overriding `_enter_tree` must call the base implementation.** Godot
  binds only the most derived override, and the base is what registers the controller with
  `World.get_controllers()`. Forgetting it removes the controller from every world list, silently.
- **A `LocalPlayer` is not a node.** It has no name path and does not appear in the scene tree; hold
  the reference or ask the `GameInstance` for it by index. It also outlives the level, which is the
  whole reason it is not one.

## Startup order — the one that costs the most time

**A level's children are ready BEFORE the game mode exists.** The order is:

```
[Level] _enter_tree / _ready      <- the level AND EVERYTHING IN IT
GameState created and added
GameMode._init_game               <- spawns and possesses the players
GameMode._ready
Level._init_level                 <- the first hook that can see all of it
```

So a HUD, a camera, a spawner, a warning overlay — anything sitting in the level scene — gets its
`_ready` **before there is a game mode to ask, before a pawn exists, and before a controller
possesses anything**. `World.get_game_mode()` returns null there, and so does anything reached
through it.

This does not fail loudly. `get_game_mode()` hands back null, the usual `if game_mode != null`
guard swallows it, and the node simply never finds what it was looking for — a HUD that shows
nothing, a camera that follows nothing, a touch control that never responds because the reference
it needed was null for the whole session.

Two ways out, and the second is usually better:

- **`Level._init_level`** runs after everything is built, so a level script can reach in and hand
  each child what it needs.
- **Resolve on first use.** Keep the reference null until something actually needs it, and look it
  up then. This also survives the pawn being respawned mid-level, which the level hook does not:

```gdscript
var _aircraft: Node

func _resolve_aircraft() -> bool:
    if _aircraft != null and is_instance_valid(_aircraft):
        return true
    var game_mode := ProjectStatics.get_game_mode(get_tree() as World)
    if game_mode == null:
        return false
    _aircraft = game_mode.pawn_of_interest
    return _aircraft != null

func _process(_delta: float) -> void:
    if not _resolve_aircraft():
        return
    ...
```

**Within one scene, a child's `_ready` still runs before its parent's.** That matters wherever a
parent sets something up that a child reads — a pawn root defining its [AttributeSet] in `_ready`
and a child sprite reading an attribute in its own. The child runs first and reads a value that
does not exist yet, which for a numeric attribute means `0.0`. Read it at the point of use, and
give the fallback a value that means "unconfigured", never one that means "do nothing".

## Draw order — a pawn is not inside the level

Pawns are parented to `/root/Pawns`, not to the level, so their node path is the same on every
peer. `World` builds that container during startup, before any level is loaded, which puts it
**earlier in root's child list than the level**:

```
root
 |- Pawns      <- the pawn
 |- Level      <- everything the level draws
```

2D children of root draw in tree order, so **the entire level, background included, paints over
the pawn**. A pawn with a plain background behind it is simply invisible, with nothing in the
Output to say why: it is present, visible, unrotated and exactly where it should be.

Give the pawn scene's root a `z_index` above what the level draws. A framework that parented the
pawn into the level got this implicitly; this one trades it for an address that is identical
everywhere.

## Game mode, level and spawning

- **`GameMode` is null on a client, on purpose.** Use `World.get_game_state()` for anything a client
  has to read, and keep rules behind `has_authority()`.
- **Every scene a game mode names has to be its own file.** A scene built inline cannot be named to
  another machine, so a client would have nothing to build. The framework warns when it finds one
  while networked.
- **A pawn built by hand is not mirrored.** Choose the scene through `default_pawn_scene`,
  `pawn_scene_overrides` or `_get_pawn_scene_for`, and let the framework spawn it; a node a script
  instantiates itself exists only where it was made.
- **The `Pawn` node is found recursively.** It may be the pawn scene root or any descendant; the
  first match wins. Two of them in one scene means the other is silently ignored.
- **Player starts are claimed once each per level.** With more players than starts the framework
  warns and reuses them. It cannot test whether a start is physically occupied.
- **Possession makes the pawn camera current, where the player is sitting.** For a game filmed by a
  camera the level owns, set `Pawn.auto_manage_camera = false`, or the pawn quietly steals the view
  — and without a camera on the pawn you pay for a recursive search that finds nothing.

## Input

- **Input binding happens on possession.** `_setup_input_component` is called by the
  `PlayerController` when it possesses the pawn — binding from `_ready` is too early and will not
  fire.
- **Never read `Input` from a pawn script.** It merges every device, so with two local players it
  moves both pawns at once, and on a server it is nobody's input at all. Go through the
  `InputComponent`, which resolves to the owning player's `PlayerInput`.
- **Raising `max_local_players` without narrowing player 0 leaves nothing for anyone else.**
  Player 0 starts on `DEVICE_SLOT_ALL` and would claim every event, including the ones a second pad
  needs to join with. The framework warns about exactly this combination.
- **`PlayerInput` in filtered mode does not see `Input.action_press()`.** Synthetic actions pushed
  into the engine singleton belong to nobody. Use `PlayerInput.action_press()` on the player you
  mean.
- **The `InputRouter` listens on `_input`, not `_unhandled_input`.** It has to: if a `Control`
  swallowed a press but not the release, the action would stay held forever. It never marks input
  handled, so the GUI is unaffected — but gameplay actions do fire while a `LineEdit` has focus.
  Disable the `InputComponent` to gate that.

## Gameplay tags

- **Tags are referenced by name, never by object.** Containers serialize a `PackedStringArray` and
  resolve through `GameplayTagsManager` on load. Never author an inline `GameplayTag` sub-resource
  in a `.tres` — it compares equal by name but is not the canonical instance, and it breaks when the
  tag is renamed.
- **Tag container properties start out null.** `ability_tags`, `granted_tags` and the rest default
  to `null`, not to an empty container, because ClassDB warns about any live object used as a
  property default. Every framework method that takes a container accepts null and reads it as
  empty, and the inspector creates one on the first edit — but your own GDScript has to check
  `is_valid()` before calling into one.
- **`request_tag()` turns a typo into a real tag.** It never returns null for a well-formed name; it
  registers the tag, warns, and carries on. `get_tag_source_kind()` returning `TAG_SOURCE_RUNTIME`
  is how you find these — the editor pickers show them in red.
- **Tag tables load before anything else.** `World::_initialize` builds `GameplayTagsManager` before
  creating the `GameInstance`, so tags are always resolvable from `_on_init` onward.

## Ability system

- **The attribute formula is `(base + sum of ADD) * product of MULTIPLY`,** and a single `OVERRIDE`
  beats all of it. `AttributeSet` is duplicated at runtime, so the resource on disk is a template
  and play never mutates it.
- **Owned tags are reference counted.** Two sources granting the same tag both have to release it.
  One ability ending does not strip a tag another still grants.
- **The ability system is not replicated.** Activate abilities where the pawn is authoritative and
  let the results reach clients through what you replicate on the pawn or its player state.
- **Attributes do not exist until something defines them.** Whatever assigns the `AttributeSet` —
  usually the pawn root in its `_ready` — runs *after* its own children's `_ready`, so a child
  reading an attribute there gets `0.0` and no warning. Read attributes at the point of use, and
  make the fallback a value that means "not configured yet" rather than one that means "do nothing"
  (a multiplier's fallback is `1.0`, never `0.0`).

## Pooling and messaging

- **A pooled node is reset by name, not by interface.** `NodePool` calls `_on_acquired()` and
  `_on_released()` if the node defines them, because a pooled node is a `RigidBody2D` one moment and
  an `Area2D` the next and there is no shared base to declare them on. A typo in either name is
  silent.
- **`NodePool` frees everything when it leaves the tree.** Nodes waiting in the free list have no
  parent, so nobody else ever would. That also means a pool moved to another parent comes back
  empty.
- **A message router listener does not have to be unregistered.** One whose object has been freed is
  dropped on the next broadcast. Delivery is synchronous and the payload is passed as
  `callback(channel, payload)` — two arguments, always, because a `MATCH_PARTIAL` listener cannot
  otherwise tell what it heard. It is local to one peer.
- **Listeners on one channel run in reverse registration order — last registered, first called.**
  The router iterates backwards so it can drop freed listeners as it goes. Never encode a dependency
  in registration order: if B needs what A produced, A should broadcast a second channel when the
  result exists, and B should listen on that.

## Saves and builds

- **The default save encryption key is in the GFGD repository.**
  `application/game_framework/save_encryption_key` defaults to it so existing saves keep working;
  changing it makes them unreadable, so change it before your first release rather than after.
- **`ProjectStatics.load_game` returns null for four different reasons.** Only `LOAD_NOT_FOUND`
  means "no save yet"; decryption failure, rejected data and a wrong script all mean a real file is
  sitting there. Ask `get_last_load_result()` — never infer "first run" from the null. The framework
  then blocks `save_game` on that slot (`ERR_LOCKED`) so the old data-loss shape fails loudly on the
  write instead of destroying the profile; `clear_load_failure(slot)` lifts it deliberately.
- **A `.tres` whose script extends a native GFGD type must name that type in its header.**
  `[gd_resource type="AttributeSet" script_class="MyAttributes" ...]`, not `type="Resource"`. Godot
  refuses to attach a script whose base is a GDExtension class to a resource declared as the wrong
  type, and the resource then loads with no script and every value at its default. The editor writes
  the right header when you create the resource by picking the GFGD type; a hand-written or
  hand-edited `.tres` is where this goes wrong.
- **The editor binary loads the `.editor` library even when running a game.**
  `godot --path <project>` from an editor build matches `windows.x86_64.single.debug.editor` in the
  manifest, not `template_debug`. Rebuilding only the template target and testing that way will
  appear to change nothing — a real template build needs an export.
