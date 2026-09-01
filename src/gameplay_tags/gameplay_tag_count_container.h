#ifndef GAMEPLAY_TAG_COUNT_CONTAINER_H
#define GAMEPLAY_TAG_COUNT_CONTAINER_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "gameplay_tags/gameplay_tag_container.h"

using namespace godot;

namespace GFGD
{
class GameplayTag;

class GameplayTagCountContainer : public Object
{
	GDCLASS(GameplayTagCountContainer, Object)

private:
	HashMap<StringName, int> gameplay_tag_count_map;
	HashMap<StringName, Vector<Callable>> gameplay_tag_event_map;
	Ref<GameplayTagContainer> explicit_tags;

public:
	GameplayTagCountContainer();

	// Returns true when the tag's existence flipped (0 -> >0 or >0 -> 0).
	bool update_tag_count(const Ref<GameplayTag>& tag, int count_delta);
	int get_tag_count(const StringName& tag_name) const;
	void register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate);

	Ref<GameplayTagContainer> get_explicit_tags() const { return explicit_tags; }

protected:
	static void _bind_methods();
};

}

#endif
