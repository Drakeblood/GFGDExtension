#include "gameplay_tag_count_container.h"
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tag.h"
#include "gameplay_tag_container.h"

using namespace godot;

namespace GFGD
{
GameplayTagCountContainer::GameplayTagCountContainer()
{
	explicit_tags.instantiate();
}

bool GameplayTagCountContainer::update_tag_count(const Ref<GameplayTag>& tag, int count_delta)
{
	if (count_delta == 0) return false;

	if (gameplay_tag_count_map.has(tag))
	{
		int current_count = gameplay_tag_count_map[tag];
		gameplay_tag_count_map[tag] = MAX(current_count + count_delta, 0);
	}
	else
	{
		gameplay_tag_count_map[tag] = MAX(count_delta, 0);
	}

	int new_count = gameplay_tag_count_map[tag];
	if (explicit_tags->has_tag_exact(tag))
	{
		if (new_count == 0)
		{
			explicit_tags->remove_tag(tag);
		}
	}
	else if (new_count != 0)
	{
		explicit_tags->add_tag(tag);
	}

	if (gameplay_tag_event_map.has(tag))
	{
		Callable tag_delegate = gameplay_tag_event_map[tag];
		if (tag_delegate.is_valid())
		{
			tag_delegate.call(new_count);
		}
	}

	return true;
}

void GameplayTagCountContainer::register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate)
{
	if (!gameplay_tag_event_map.has(tag))
	{
		gameplay_tag_event_map[tag] = tag_delegate;
		return;
	}

	Callable existing_delegate = gameplay_tag_event_map[tag];
	gameplay_tag_event_map[tag] = Callable(existing_delegate).bind_front(tag_delegate);
}

void GameplayTagCountContainer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("update_tag_count", "tag", "count_delta"), &GameplayTagCountContainer::update_tag_count);
	ClassDB::bind_method(D_METHOD("register_gameplay_tag_event", "tag", "tag_delegate"), &GameplayTagCountContainer::register_gameplay_tag_event);
	ClassDB::bind_method(D_METHOD("get_explicit_tags"), &GameplayTagCountContainer::get_explicit_tags);
}
}