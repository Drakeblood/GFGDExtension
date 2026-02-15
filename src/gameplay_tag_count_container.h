#ifndef GAMEPLAY_TAG_COUNT_CONTAINER_H
#define GAMEPLAY_TAG_COUNT_CONTAINER_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/callable.hpp>
#include <godot_cpp/classes/dictionary.hpp>

using namespace godot;

namespace GFGD 
{
class GameplayTag;
class GameplayTagContainer;

class GameplayTagCountContainer : public Object
{
	GDCLASS(GameplayTagCountContainer, Object)

private:
	Dictionary gameplay_tag_count_map;
	Dictionary gameplay_tag_event_map;
	Ref<GameplayTagContainer> explicit_tags;

public:
	GameplayTagCountContainer();

	bool update_tag_count(const Ref<GameplayTag>& tag, int count_delta);
	void register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate);

	Ref<GameplayTagContainer> get_explicit_tags() const { return explicit_tags; }

protected:
	static void _bind_methods();
};

}

#endif