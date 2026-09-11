# Getting an exact signature, fast

GFGD's API is fully introspectable. **Never guess a signature** — three sources give it exactly, and
all three come from the same build, so they cannot disagree.

## 1. The XML reference (fastest, offline)

`addons/gfgdextension/doc_classes/<Class>.xml` — one file per public class, 37 in total. These are
the same files compiled into the editor and `template_debug` binaries, so what F1 shows a human is
what this file says.

```
grep -A4 'method name="server_travel"' addons/gfgdextension/doc_classes/World.xml
```

The XML carries, per class: `inherits`, every `<method>` with typed `<param>`s, defaults and
`qualifiers="const"`, every `<signal>` with its parameters, every `<member>` (property) with type
and default, and every `<constant>` grouped by `enum`.

To find which class a method lives on:

```
grep -l 'method name="try_activate_ability"' addons/gfgdextension/doc_classes/*.xml
```

To list a class's whole surface at a glance:

```
grep -E '<(method|signal|member|constant) name=' addons/gfgdextension/doc_classes/Pawn.xml
```

## 2. The C++ sources (when behaviour matters)

`addons/gfgdextension/src/**` ships with the addon. Reach for it when the question is *what does
this actually do*, not *what is its signature* — ordering, fallbacks, what gets warned about, what
a virtual is called with.

```
src/framework/      world, game_instance, local_player, player_input, input_router,
                    input_component, game_state_base, player_state, game_mode_base, level,
                    controller, player_controller, ai_controller, pawn, net_driver,
                    net_replication, player_start_2d/3d, node_pool, player_virtual_joystick,
                    player_touch_button, gameplay_message_router, save_game
src/gameplay_tags/  gameplay_tag, gameplay_tag_container, gameplay_tag_count_container,
                    gameplay_tag_table, gameplay_tags_manager
src/ability_system/ attribute_set, attribute_modifier, gameplay_effect, active_gameplay_effect,
                    gameplay_ability, ability_system_component
src/core/           assert, assertion_exception, assertion_messages, project_statics
src/editor/         TOOLS_ENABLED only — tag tree, pickers, inspector plugin
src/register_types.cpp   every registered class, and the project settings the extension adds
```

`register_types.cpp` is the definitive list of what exists: the `GDREGISTER_CLASS` block, and the
`register_gfgd_settings()` function that declares every `application/game_framework/*` setting with
its type, hint and default.

`_bind_methods()` in each class's `.cpp` is where the GDScript-visible surface is declared — method
names, argument names and defaults, signals, properties and their setters/getters. If a method is
not in `_bind_methods`, GDScript cannot call it.

## 3. Live introspection (when the editor is running)

Through the Godot MCP toolkit's `execute_code`:

```gdscript
for m in ClassDB.class_get_method_list("AbilitySystemComponent", true):
	print(m.name, " ", m.args)

print(ClassDB.class_get_signal_list("World", true))
print(ClassDB.class_get_property_list("Pawn", true))
print(ClassDB.class_get_integer_constant_list("PlayerInput", true))
```

Pass `true` as the second argument to get only the class's own members rather than everything it
inherits from `Node`/`Object`.

This is the only source that proves the extension is actually **loaded** — if
`ClassDB.class_exists("World")` is false, the binary did not load and nothing else in this skill
applies yet.

## What none of the three tell you

Semantics that are not expressible as a signature — when a virtual is called, what falls back to
what, what warns instead of failing. Those are in `gotchas.md` and the other reference files here,
and in `addons/gfgdextension/README.md`. **Read `gotchas.md` before debugging.**

## Which build am I looking at

`addons/gfgdextension/VERSION.txt` — the GFGD commit, the packing date, the godot-cpp version, the
`api_version` the binary was built against and the minimum Godot version. If it is absent, the addon
predates version stamping and was copied in by hand.
