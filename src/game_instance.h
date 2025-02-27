#ifndef GAME_INSTANCE_H
#define GAME_INSTANCE_H

#include <godot_cpp/classes/object.hpp>

namespace godot
{
class GameInstance : public Object
{
	GDCLASS(GameInstance, Object)

public:
	GameInstance();
	~GameInstance();

protected:
	static void _bind_methods();

};
}

#endif