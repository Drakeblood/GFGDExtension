#include "save_game.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
SaveGame::SaveGame()
{
	
}

SaveGame::~SaveGame()
{
	
}

String SaveGame::to_json() const
{
	return "{}";
}

Error SaveGame::from_json(const Variant& data)
{
	return OK;
}

void SaveGame::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("to_json"), &SaveGame::to_json);
	ClassDB::bind_method(D_METHOD("from_json", "data"), &SaveGame::from_json);
}
}