#ifndef GAMEPLAY_CUE_H
#define GAMEPLAY_CUE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

namespace GFGD
{
// A gameplay cue handler with no instance of its own - Unreal's
// GameplayCueNotify_Static. One resource answers every cue with its tag, on
// every character, so it keeps no state: play a sound at the target, flash its
// material, spawn something that looks after itself.
//
// For a cue that needs a node living as long as it is active - a burning fire
// on the character, a shield bubble - map the tag to a PackedScene instead; see
// GameplayCueTable.
class GameplayCueNotify : public Resource
{
	GDCLASS(GameplayCueNotify, Resource)

public:
	void execute(Node* target, const Dictionary& parameters);
	void on_active(Node* target, const Dictionary& parameters);
	void on_removed(Node* target, const Dictionary& parameters);

	GDVIRTUAL2(_on_execute, Node*, Dictionary)
	GDVIRTUAL2(_on_active, Node*, Dictionary)
	GDVIRTUAL2(_on_removed, Node*, Dictionary)

protected:
	static void _bind_methods();
};

// Cue tag -> handler: a GameplayCueNotify, or a PackedScene. Listed in the
// application/game_framework/gameplay_cue_tables project setting, like the tag
// tables are.
class GameplayCueTable : public Resource
{
	GDCLASS(GameplayCueTable, Resource)

private:
	Dictionary cues;

public:
	Dictionary get_cues() const { return cues; }
	void set_cues(const Dictionary& value) { cues = value; }

protected:
	static void _bind_methods();
};

// Finds the handler for a cue tag - Unreal's GameplayCueManager, minus the
// asset scanning: the handlers are whatever the cue tables name.
//
// A tag with no handler of its own falls back to its parent's, so
// "Cue.Hit.Fire" plays "Cue.Hit"'s handler until it gets one. Answers are
// cached; add_table clears the cache.
class GameplayCueManager : public Object
{
	GDCLASS(GameplayCueManager, Object)

private:
	static GameplayCueManager* instance;

	Dictionary handlers;
	HashMap<StringName, Variant> resolved;
	bool loaded;

public:
	static GameplayCueManager* get_singleton();
	static void destroy_singleton();

	GameplayCueManager();

	// The handler for this tag or its nearest parent that has one; null when
	// none does.
	Ref<Resource> find_handler(const StringName& cue_tag);

	// Adds a table at runtime, over what the project setting listed. Later
	// tables win on a duplicate tag.
	void add_table(const Ref<GameplayCueTable>& table);
	void clear();

protected:
	static void _bind_methods();

private:
	void load_project_tables();
};

}

#endif
