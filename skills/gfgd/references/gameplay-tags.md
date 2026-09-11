# Gameplay tags

Hierarchical, dot-separated names — `Status.Debuff.Burning` — declared as data and resolved through
one runtime lookup.

## Declaring them

Tags live in **`GameplayTagTable`** resources, a `tag name -> description` dictionary, listed in
`application/game_framework/gameplay_tag_tables`. Several tables merge in listed order; on a
duplicate declaration the first one wins and the later one warns.

**Parents are implicit.** Declaring `Ability.Combat.Melee` also registers `Ability` and
`Ability.Combat` as usable tags, though they hold no description until some table declares them
outright.

Selecting a table in the FileSystem dock opens a tree editor. Double-click a row (or press Rename)
to edit a name or a description; renaming a tag rewrites everything beneath it as a single undo
step. `GameplayTag` and `GameplayTagContainer` properties get a hierarchical picker in the
inspector.

`GameplayTagTable` API: `set_tags` / `get_tags`, `has_tag(name)`, `get_description(name)`,
`get_tag_names()`.

**Tag tables load before anything else.** `World::_initialize` builds `GameplayTagsManager` before
creating the `GameInstance`, so tags are always resolvable from `GameInstance._on_init` onward.

## Looking them up

```gdscript
var manager := GameplayTagsManager.get_singleton()
var tag := manager.request_tag(&"Status.Debuff.Burning")

if asc.get_owned_gameplay_tags().has_tag(tag):
	pass    # has_tag is hierarchical: "Status.Debuff" would match too
```

`GameplayTagsManager` (static `get_singleton()`, not an autoload):

```gdscript
has_tag(name) -> bool
get_tag(name) -> GameplayTag          # null if unknown
request_tag(name) -> GameplayTag      # registers it if unknown - see below
get_separated_tag(name)
get_all_tag_names() -> ...
get_tag_source_kind(name) -> int      # TAG_SOURCE_TABLE | TAG_SOURCE_IMPLICIT | TAG_SOURCE_RUNTIME
get_tag_source_path(name) -> String
get_tag_description(name) -> String
get_loaded_table_paths() -> ...
initialize_tags() -> void
is_valid_tag_name(name) -> bool       # static
```

**`request_tag()` turns a typo into a real tag.** It never returns null for a well-formed name; it
registers the tag, warns, and carries on. `get_tag_source_kind()` returning `TAG_SOURCE_RUNTIME` is
how you find these — the editor pickers show them in red. Use `has_tag()` / `get_tag()` when you
want a miss to stay a miss.

## Containers

`GameplayTagContainer` is a `Resource` holding `tags: PackedStringArray`.

```gdscript
add_tag(tag: GameplayTag)      add_tag_by_name(name: StringName)
remove_tag(tag)
has_tag(tag)                   # hierarchical: parents match children
has_tag_exact(tag)
has_any(container)             has_all(container)    has_all_exact(container)
get_length()                   get_tag(index)
```

Two rules that cause real bugs:

- **Tags are referenced by name, never by object.** Containers serialize a `PackedStringArray` and
  resolve through the manager on load. **Never author an inline `GameplayTag` sub-resource in a
  `.tres`** — it compares equal by name but is not the canonical instance, and it breaks when the
  tag is renamed.
- **Tag container properties start out `null`, not empty**, because ClassDB warns about any live
  object used as a property default. Framework methods accept null and read it as empty; the
  inspector creates one on the first edit; **your own GDScript has to check `is_valid()`** before
  calling into one.

```gdscript
if granted_tags != null and granted_tags.is_valid() and granted_tags.has_tag(tag):
	...
```

## Counted tags

`GameplayTagCountContainer` is the reference-counted form the ability system uses for owned tags:

```gdscript
update_tag_count(tag: GameplayTag, count_delta: int)
get_tag_count(name: StringName) -> int
register_gameplay_tag_event(tag: GameplayTag, delegate: Callable)
get_explicit_tags()
```

**Owned tags are reference counted.** Two sources granting the same tag both have to release it; one
ability ending does not strip a tag another still grants.

## Naming them

Tag names are the API between systems, so pick a shape and keep it. Common shapes:

```
Ability.Combat.Melee          what an ability is
Status.Debuff.Burning         a condition on an actor
Event.Player.Died             something that happened
Fx.Impact.Metal               a message-router channel (see pooling-and-messaging.md)
Input.Blocked.Menu            a gate
```

`GameplayMessageRouter` channels are tag names too, and a listener on `Fx` with `MATCH_PARTIAL`
hears `Fx.Impact` — so a tag hierarchy designed once serves both the ability system and the message
bus.
