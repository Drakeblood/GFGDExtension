#include "debug/gameplay_debugger.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/collision_object3d.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "debug/console_manager.h"
#include "debug/debug_camera.h"
#include "debug/debug_replicator.h"
#include "debug/debug_tools.h"
#include "framework/pawn.h"
#include "framework/player_input.h"
#include "framework/player_controller.h"
#include "framework/world.h"

using namespace GFGD;

GameplayDebugger* GameplayDebugger::singleton = nullptr;

namespace
{
const char* CATEGORIES_SETTING = "application/game_framework/debug/gameplay_debugger_categories";
const char* KEY_HINTS_SETTING = "application/game_framework/debug/gameplay_debugger_key_hints";
const char* COLOR_HEAD = "#ffd36e";
const char* COLOR_DIM = "#9aa4b1";
const char* COLOR_GOOD = "#8fe388";
}

// --- Registry -----------------------------------------------------------------

GameplayDebugger::GameplayDebugger()
{
	initialized = false;
}

void GameplayDebugger::create_singleton()
{
	if (singleton != nullptr) { return; }
	singleton = memnew(GameplayDebugger);
	Engine::get_singleton()->register_singleton("GameplayDebugger", singleton);
}

void GameplayDebugger::destroy_singleton()
{
	if (singleton == nullptr) { return; }
	Engine::get_singleton()->unregister_singleton("GameplayDebugger");
	singleton->categories.clear();
	memdelete(singleton);
	singleton = nullptr;
}

void GameplayDebugger::ensure_initialized()
{
	if (initialized) { return; }
	initialized = true;

	// Built-ins first, so their slots never move when a game adds its own.
	Vector<Ref<GameplayDebuggerCategory>> built_in;
	built_in.push_back(memnew(GameplayDebuggerCategoryBasic));
	built_in.push_back(memnew(GameplayDebuggerCategoryAbilities));
	built_in.push_back(memnew(GameplayDebuggerCategoryMovement));
	built_in.push_back(memnew(GameplayDebuggerCategoryNet));
	// Anything registered before the first use keeps its place after them.
	Vector<Ref<GameplayDebuggerCategory>> early = categories;
	categories = built_in;
	for (const Ref<GameplayDebuggerCategory>& category : early) { register_category(category); }

	const PackedStringArray paths = ProjectSettings::get_singleton()->get_setting(CATEGORIES_SETTING, PackedStringArray());
	for (int i = 0; i < paths.size(); i++)
	{
		const String path = paths[i].strip_edges();
		if (path.is_empty()) { continue; }
		Ref<Script> script = ResourceLoader::get_singleton()->load(path);
		if (script.is_null())
		{
			WARN_PRINT(vformat("GFGD: could not load gameplay debugger category \"%s\".", path));
			continue;
		}
		Variant instance = script->call("new");
		Ref<GameplayDebuggerCategory> category = instance;
		if (category.is_null())
		{
			WARN_PRINT(vformat("GFGD: \"%s\" does not extend GameplayDebuggerCategory.", path));
			continue;
		}
		if (category->get_category_name().is_empty())
		{
			category->set_category_name(path.get_file().get_basename().capitalize());
		}
		register_category(category);
	}
}

void GameplayDebugger::release_script_references()
{
	categories.clear();
	initialized = false;
}

void GameplayDebugger::register_category(const Ref<GameplayDebuggerCategory>& category)
{
	ERR_FAIL_COND(category.is_null());
	ERR_FAIL_COND_MSG(category->get_category_name().is_empty(), "GameplayDebugger: a category needs a category_name.");

	for (int i = 0; i < categories.size(); i++)
	{
		if (categories[i]->get_category_name().nocasecmp_to(category->get_category_name()) == 0)
		{
			categories.write[i] = category;
			return;
		}
	}
	categories.push_back(category);
}

void GameplayDebugger::unregister_category(const String& name)
{
	for (int i = 0; i < categories.size(); i++)
	{
		if (categories[i]->get_category_name().nocasecmp_to(name) == 0)
		{
			categories.remove_at(i);
			return;
		}
	}
}

Ref<GameplayDebuggerCategory> GameplayDebugger::get_category(const String& name)
{
	ensure_initialized();
	for (const Ref<GameplayDebuggerCategory>& category : categories)
	{
		if (category->get_category_name().nocasecmp_to(name) == 0) { return category; }
	}
	return Ref<GameplayDebuggerCategory>();
}

TypedArray<GameplayDebuggerCategory> GameplayDebugger::get_categories()
{
	ensure_initialized();
	TypedArray<GameplayDebuggerCategory> result;
	for (const Ref<GameplayDebuggerCategory>& category : categories) { result.push_back(category); }
	return result;
}

PackedStringArray GameplayDebugger::get_category_names()
{
	ensure_initialized();
	PackedStringArray names;
	for (const Ref<GameplayDebuggerCategory>& category : categories) { names.push_back(category->get_category_name()); }
	return names;
}

Dictionary GameplayDebugger::collect(Node* target, const PackedStringArray& category_names, PlayerController* viewer)
{
	ensure_initialized();
	Dictionary result;
	for (int i = 0; i < category_names.size(); i++)
	{
		Ref<GameplayDebuggerCategory> category = get_category(category_names[i]);
		if (category.is_null()) { continue; }
		result[category->get_category_name()] = category->collect_data(target, viewer);
	}
	result["_target_found"] = target != nullptr;
	return result;
}

void GameplayDebugger::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("register_category", "category"), &GameplayDebugger::register_category);
	ClassDB::bind_method(D_METHOD("unregister_category", "name"), &GameplayDebugger::unregister_category);
	ClassDB::bind_method(D_METHOD("get_category", "name"), &GameplayDebugger::get_category);
	ClassDB::bind_method(D_METHOD("get_categories"), &GameplayDebugger::get_categories);
	ClassDB::bind_method(D_METHOD("get_category_names"), &GameplayDebugger::get_category_names);
	ClassDB::bind_method(D_METHOD("collect", "target", "category_names", "viewer"), &GameplayDebugger::collect);
}

// --- HUD ----------------------------------------------------------------------

GameplayDebuggerHUD::GameplayDebuggerHUD()
{
	active = false;
	locked = false;
	never_shown = true;
	enabled_initialized = false;
	panel = nullptr;
	label = nullptr;
	text_accumulator = 0.0;
	local_collect_accumulator = 0.0;
	aim_lock_pending = false;
	aim_accumulator = 0.0;
	sent_active = false;
	mouse_mode_before = Input::MOUSE_MODE_VISIBLE;
}

void GameplayDebuggerHUD::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	set_layer(121);
	panel = memnew(PanelContainer);
	panel->set_name("Panel");
	// Left edge, vertically centred: the ability system debugger owns the right.
	panel->set_anchors_and_offsets_preset(Control::PRESET_CENTER_LEFT, Control::PRESET_MODE_MINSIZE, 12);
	panel->set_v_grow_direction(Control::GROW_DIRECTION_BOTH);
	panel->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(Color(0.05, 0.06, 0.08, 0.82));
	style->set_corner_radius_all(6);
	style->set_content_margin_all(10);
	panel->add_theme_stylebox_override("panel", style);
	add_child(panel);

	label = memnew(RichTextLabel);
	label->set_use_bbcode(true);
	label->set_fit_content(true);
	label->set_scroll_active(false);
	label->set_custom_minimum_size(Vector2(520, 0));
	label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	label->add_theme_font_size_override("normal_font_size", 13);
	label->add_theme_font_size_override("bold_font_size", 13);
	panel->add_child(label);
	panel->set_visible(false);

	if (ConsoleManager* console = ConsoleManager::get_singleton())
	{
		console->register_command("gd.toggle", callable_mp(this, &GameplayDebuggerHUD::cmd_toggle), "Shows or hides the gameplay debugger.", ConsoleManager::FLAG_RAW_ARGS);
		console->register_command("gd.category", callable_mp(this, &GameplayDebuggerHUD::cmd_category), "Toggles a gameplay debugger category by name or slot; no argument lists them.", ConsoleManager::FLAG_RAW_ARGS);
		console->register_command("gd.target", callable_mp(this, &GameplayDebuggerHUD::cmd_target), "Debugs: self | aim | <node path>. Locks the target.", ConsoleManager::FLAG_RAW_ARGS);
		console->register_command("gd.lock", callable_mp(this, &GameplayDebuggerHUD::cmd_lock), "Locks or unlocks the current target.", ConsoleManager::FLAG_RAW_ARGS);
		console->register_command("gd.freecam", callable_mp(this, &GameplayDebuggerHUD::cmd_freecam), "Free camera on/off (WASD QE, mouse, Shift/Ctrl, wheel = speed).", ConsoleManager::FLAG_RAW_ARGS);
		console->set_command_usage("gd.category", "[name|slot]");
		console->set_command_usage("gd.target", "self|aim|<node path>");
	}

	set_process(false);
	set_process_input(true);
	set_physics_process(true);
}

void GameplayDebuggerHUD::_exit_tree()
{
	if (ConsoleManager* console = ConsoleManager::get_singleton())
	{
		console->unregister_command("gd.toggle");
		console->unregister_command("gd.category");
		console->unregister_command("gd.target");
		console->unregister_command("gd.lock");
		console->unregister_command("gd.freecam");
	}
	if (is_free_camera()) { PlayerInput::unblock_gameplay_input(); }
}

void GameplayDebuggerHUD::init_enabled_categories()
{
	if (enabled_initialized || GameplayDebugger::get_singleton() == nullptr) { return; }
	enabled_initialized = true;
	const TypedArray<GameplayDebuggerCategory> categories = GameplayDebugger::get_singleton()->get_categories();
	for (int i = 0; i < categories.size(); i++)
	{
		Ref<GameplayDebuggerCategory> category = categories[i];
		if (category.is_valid() && category->is_enabled_by_default()) { enabled_categories.push_back(category->get_category_name()); }
	}
}

void GameplayDebuggerHUD::_input(const Ref<InputEvent>& event)
{
	ConsoleManager* console = ConsoleManager::get_singleton();
	if (console != nullptr && console->is_console_open()) { return; }

	if (DebugTools::is_action_pressed(event, DebugTools::ACTION_GAMEPLAY_DEBUGGER))
	{
		set_active(!active);
		get_viewport()->set_input_as_handled();
		return;
	}
	if (is_free_camera() && DebugTools::is_action_pressed(event, DebugTools::ACTION_FREE_CAMERA_TELEPORT))
	{
		teleport_pawn_to_free_camera();
		get_viewport()->set_input_as_handled();
		return;
	}
	if (!active) { return; }

	if (DebugTools::is_action_pressed(event, DebugTools::ACTION_FREE_CAMERA))
	{
		set_free_camera(!is_free_camera());
		get_viewport()->set_input_as_handled();
		return;
	}

	if (DebugTools::is_action_pressed(event, DebugTools::ACTION_GAMEPLAY_DEBUGGER_LOCK))
	{
		locked = !locked;
		get_viewport()->set_input_as_handled();
		refresh_text();
		return;
	}
	for (int slot = 0; slot < DebugTools::CATEGORY_SLOTS; slot++)
	{
		if (DebugTools::is_action_pressed(event, DebugTools::category_action(slot)))
		{
			toggle_category_slot(slot);
			get_viewport()->set_input_as_handled();
			return;
		}
	}
}

void GameplayDebuggerHUD::set_active(bool value)
{
	active = value;
	init_enabled_categories();
	if (panel != nullptr) { panel->set_visible(value); }
	set_process(value);
	if (value)
	{
		// Opened for the first time: what is usually wanted is the player's own
		// character, not whatever the camera happens to be looking past. Pinned,
		// so the aim ray does not take it away a moment later. A target chosen
		// before that (gd.target) is kept. Without a pawn yet (a menu) the next
		// opening tries again.
		if (never_shown && get_target() == nullptr)
		{
			Node* own = default_target();
			if (own != nullptr)
			{
				set_target(own);
				locked = true;
			}
		}
		if (get_target() == nullptr) { set_target(default_target()); }
		if (get_target() != nullptr) { never_shown = false; }
		sync_request();
		refresh_text();
	}
	else
	{
		set_free_camera(false);
		sync_request();
	}
}

DebugCamera* GameplayDebuggerHUD::get_free_camera() const
{
	return Object::cast_to<DebugCamera>(ObjectDB::get_instance(free_camera_id));
}

bool GameplayDebuggerHUD::is_free_camera() const
{
	return get_free_camera() != nullptr;
}

void GameplayDebuggerHUD::set_free_camera(bool value)
{
	if (value == is_free_camera() || !is_inside_tree()) { return; }
	Input* input = Input::get_singleton();

	if (value)
	{
		Camera3D* current = get_viewport()->get_camera_3d();
		DebugCamera* camera = memnew(DebugCamera);
		camera->set_name("DebugCamera");
		// Under the root, not under this CanvasLayer, so it lives in the main 3D world.
		get_tree()->get_root()->add_child(camera);
		if (current != nullptr) { camera->start_from(current->get_global_transform(), current->get_fov()); }
		camera->make_current();

		previous_camera_id = current != nullptr ? current->get_instance_id() : ObjectID();
		free_camera_id = camera->get_instance_id();
		PlayerInput::block_gameplay_input();
		mouse_mode_before = input->get_mouse_mode();
		input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
	}
	else
	{
		DebugCamera* camera = get_free_camera();
		free_camera_id = ObjectID();
		Camera3D* previous = Object::cast_to<Camera3D>(ObjectDB::get_instance(previous_camera_id));
		if (previous != nullptr && previous->is_inside_tree())
		{
			previous->make_current();
		}
		else if (PlayerController* pc = DebugTools::get_local_player_controller())
		{
			// The camera it replaced is gone (the pawn respawned): back to the pawn's.
			pc->set_pawn_camera_node_as_current();
		}
		if (camera != nullptr)
		{
			camera->clear_current(false);
			camera->queue_free();
		}
		PlayerInput::unblock_gameplay_input();
		input->set_mouse_mode(mouse_mode_before);
	}
	refresh_text();
}

void GameplayDebuggerHUD::teleport_pawn_to_free_camera()
{
	DebugCamera* camera = get_free_camera();
	ConsoleManager* console = ConsoleManager::get_singleton();
	if (camera == nullptr || console == nullptr) { return; }

	const Vector3 at = camera->get_global_position();
	set_free_camera(false);
	console->execute("teleport " + String::num(at.x, 3) + "," + String::num(at.y, 3) + "," + String::num(at.z, 3));
}

Node* GameplayDebuggerHUD::get_target() const
{
	return Object::cast_to<Node>(ObjectDB::get_instance(target_id));
}

void GameplayDebuggerHUD::set_target(Node* node)
{
	target_id = node != nullptr ? node->get_instance_id() : ObjectID();
}

Node* GameplayDebuggerHUD::default_target() const
{
	PlayerController* pc = DebugTools::get_local_player_controller();
	Pawn* pawn = pc != nullptr ? pc->get_pawn() : nullptr;
	return pawn != nullptr ? pawn->get_pawn_root() : nullptr;
}

Node* GameplayDebuggerHUD::resolve_actor(Node* node)
{
	if (node == nullptr) { return nullptr; }
	SceneTree* tree = node->get_tree();
	Node* scene = tree != nullptr ? tree->get_current_scene() : nullptr;

	// Walks up to the scene (or the level under the root window, when the
	// level is not the current scene) - never into the root window itself.
	// Only a node and its direct children are looked at on the way: a deep
	// search on every ancestor would end up searching the whole tree.
	for (Node* current = node; current != nullptr && current != scene && !Object::cast_to<Viewport>(current); current = current->get_parent())
	{
		if (current->is_in_group("gameplay_debugger_target")) { return current; }
		if (Object::cast_to<Pawn>(current) != nullptr) { return current; }
		const int count = current->get_child_count();
		for (int i = 0; i < count; i++)
		{
			if (Object::cast_to<Pawn>(current->get_child(i)) != nullptr) { return current; }
		}
	}
	for (Node* current = node; current != nullptr && current != scene && !Object::cast_to<Viewport>(current); current = current->get_parent())
	{
		if (current->get_script().get_type() != Variant::NIL) { return current; }
	}
	return node;
}

Node* GameplayDebuggerHUD::find_target_under_crosshair() const
{
	// Outside the physics step the space cannot be queried: the last step's answer.
	if (!Engine::get_singleton()->is_in_physics_frame())
	{
		return Object::cast_to<Node>(ObjectDB::get_instance(aimed_id));
	}

	Viewport* viewport = get_viewport();
	Camera3D* camera = viewport != nullptr ? viewport->get_camera_3d() : nullptr;
	if (camera == nullptr || camera->get_world_3d().is_null()) { return nullptr; }

	const Vector2 center = viewport->get_visible_rect().size * 0.5;
	const Vector3 from = camera->project_ray_origin(center);
	const Vector3 to = from + camera->project_ray_normal(center) * 200.0;

	Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(from, to);
	// Skip the local pawn, which a third-person camera looks over - but not
	// from the free camera, which may well be looking at it.
	TypedArray<RID> exclude;
	Node* own = is_free_camera() ? nullptr : default_target();
	if (own != nullptr)
	{
		TypedArray<Node> bodies = own->find_children("*", "CollisionObject3D", true, false);
		if (CollisionObject3D* self_body = Object::cast_to<CollisionObject3D>(own)) { exclude.push_back(self_body->get_rid()); }
		for (int i = 0; i < bodies.size(); i++)
		{
			CollisionObject3D* body = Object::cast_to<CollisionObject3D>(bodies[i]);
			if (body != nullptr) { exclude.push_back(body->get_rid()); }
		}
	}
	query->set_exclude(exclude);

	PhysicsDirectSpaceState3D* space = camera->get_world_3d()->get_direct_space_state();
	if (space == nullptr) { return nullptr; }
	const Dictionary hit = space->intersect_ray(query);
	if (hit.is_empty()) { return nullptr; }
	Object* collider = hit.get("collider", Variant());
	return resolve_actor(Object::cast_to<Node>(collider));
}

void GameplayDebuggerHUD::toggle_category(const String& name)
{
	GameplayDebugger* debugger = GameplayDebugger::get_singleton();
	Ref<GameplayDebuggerCategory> category = debugger != nullptr ? debugger->get_category(name) : Ref<GameplayDebuggerCategory>();
	if (category.is_null()) { return; }
	init_enabled_categories();
	const String canonical = category->get_category_name();
	const int index = enabled_categories.find(canonical);
	if (index >= 0) { enabled_categories.remove_at(index); }
	else { enabled_categories.push_back(canonical); }
	sync_request();
	refresh_text();
}

void GameplayDebuggerHUD::toggle_category_slot(int slot)
{
	GameplayDebugger* debugger = GameplayDebugger::get_singleton();
	if (debugger == nullptr) { return; }
	const PackedStringArray names = debugger->get_category_names();
	if (slot >= 0 && slot < names.size()) { toggle_category(names[slot]); }
}

void GameplayDebuggerHUD::sync_request()
{
	Node* target = get_target();
	const NodePath path = target != nullptr && target->is_inside_tree() ? target->get_path() : NodePath();
	if (sent_active == active && sent_target == path && sent_categories == enabled_categories) { return; }

	sent_active = active;
	sent_target = path;
	sent_categories = enabled_categories;

	PlayerController* pc = DebugTools::get_local_player_controller();
	DebugReplicator* replicator = DebugReplicator::find_for(pc);
	if (replicator != nullptr) { replicator->request_view(active, path, enabled_categories); }
	local_collect_accumulator = 1e9; // collect locally right away when there is no replicator
}

Dictionary GameplayDebuggerHUD::current_data(bool& waiting) const
{
	waiting = false;
	DebugReplicator* replicator = DebugReplicator::find_for(DebugTools::get_local_player_controller());
	if (replicator == nullptr) { return local_data; }
	if (replicator->get_view_data_target() != sent_target || replicator->get_view_data_age() < 0.0)
	{
		waiting = true;
		return Dictionary();
	}
	return replicator->get_view_data();
}

void GameplayDebuggerHUD::_physics_process(double delta)
{
	// The aim ray lives here: the physics space can only be queried during the
	// physics step (with physics on its own thread, not at all outside it).
	if (!active && !aim_lock_pending) { return; }

	aim_accumulator += delta;
	if (!aim_lock_pending && aim_accumulator < 0.1) { return; }
	aim_accumulator = 0.0;

	Node* aimed = find_target_under_crosshair();
	aimed_id = aimed != nullptr ? aimed->get_instance_id() : ObjectID();

	if (aim_lock_pending)
	{
		aim_lock_pending = false;
		ConsoleManager* console = ConsoleManager::get_singleton();
		if (aimed == nullptr)
		{
			if (console != nullptr) { console->print_error("gd.target: nothing under the crosshair."); }
			return;
		}
		set_target(aimed);
		locked = true;
		if (!active) { set_active(true); }
		sync_request();
		if (console != nullptr) { console->print_line("Debugging " + String(aimed->get_path())); }
		return;
	}

	if (!locked && aimed != nullptr) { set_target(aimed); }
}

void GameplayDebuggerHUD::_process(double delta)
{
	if (!active) { return; }

	if (get_target() == nullptr) { set_target(default_target()); }
	sync_request();

	// No PlayerController to ask the server through: a menu, or a game that
	// does not use one. Collect here instead.
	if (DebugReplicator::find_for(DebugTools::get_local_player_controller()) == nullptr)
	{
		local_collect_accumulator += delta;
		const double interval = ProjectSettings::get_singleton()->get_setting("application/game_framework/debug/gameplay_debugger_refresh_interval", 0.15);
		if (local_collect_accumulator >= interval && GameplayDebugger::get_singleton() != nullptr)
		{
			local_collect_accumulator = 0.0;
			local_data = GameplayDebugger::get_singleton()->collect(get_target(), enabled_categories, nullptr);
		}
	}

	bool waiting = false;
	const Dictionary data = current_data(waiting);
	GameplayDebugger* debugger = GameplayDebugger::get_singleton();
	if (debugger != nullptr)
	{
		for (int i = 0; i < enabled_categories.size(); i++)
		{
			Ref<GameplayDebuggerCategory> category = debugger->get_category(enabled_categories[i]);
			if (category.is_valid())
			{
				category->draw_world(get_target(), data.get(category->get_category_name(), Dictionary()));
			}
		}
	}

	text_accumulator += delta;
	if (text_accumulator >= 0.1)
	{
		text_accumulator = 0.0;
		refresh_text();
	}
}

void GameplayDebuggerHUD::refresh_text()
{
	if (label == nullptr || !active) { return; }
	GameplayDebugger* debugger = GameplayDebugger::get_singleton();
	if (debugger == nullptr) { return; }

	const String head = String("[color=") + COLOR_HEAD + "][b]";
	const String end_head = "[/b][/color]";
	String out = head + String("GAMEPLAY DEBUGGER") + end_head + String("\n");
	// The key hints can be turned off when the game lists these keys elsewhere (its own controls screen).
	const bool key_hints = bool(ProjectSettings::get_singleton()->get_setting(KEY_HINTS_SETTING, true));
	if (key_hints)
	{
		out += String("[color=") + COLOR_DIM + "]" + DebugTools::describe_action(DebugTools::ACTION_GAMEPLAY_DEBUGGER) + " close   "
			+ DebugTools::describe_action(DebugTools::ACTION_GAMEPLAY_DEBUGGER_LOCK) + " lock target   "
			+ DebugTools::describe_action(DebugTools::category_action(0)) + "-" + DebugTools::describe_action(DebugTools::category_action(DebugTools::CATEGORY_SLOTS - 1)).replace("Num ", "") + " categories   "
			+ DebugTools::describe_action(DebugTools::ACTION_FREE_CAMERA) + " free camera[/color]\n";
	}
	if (is_free_camera())
	{
		out += String("[color=") + COLOR_GOOD + "]free camera[/color]";
		if (key_hints)
		{
			out += String("  [color=") + COLOR_DIM + "]WASD QE, mouse, Shift/Ctrl, wheel = speed, "
				+ DebugTools::describe_action(DebugTools::ACTION_FREE_CAMERA_TELEPORT) + " = move your pawn here[/color]";
		}
		out += "\n";
	}

	Node* target = get_target();
	bool waiting = false;
	const Dictionary data = current_data(waiting);
	String target_line = "target: " + (target != nullptr ? "[b]" + String(target->get_name()) + "[/b]  " + String(target->get_path()) : String("none"));
	if (locked) { target_line += String("  [color=") + COLOR_GOOD + "](locked)[/color]"; }
	out += target_line + "\n";

	DebugReplicator* replicator = DebugReplicator::find_for(DebugTools::get_local_player_controller());
	if (replicator == nullptr) { out += String("[color=") + COLOR_DIM + "]collected locally (no PlayerController)[/color]\n"; }
	else if (waiting) { out += String("[color=") + COLOR_DIM + "]waiting for the server...[/color]\n"; }
	else
	{
		out += String("[color=") + COLOR_DIM + "]server data " + String::num(replicator->get_view_data_age(), 2) + " s old[/color]\n";
		if (target != nullptr && !bool(data.get("_target_found", true)))
		{
			out += "[color=#ff8a7a]the server has no node at this path[/color]\n";
		}
	}

	// The slot bar: what each numpad key does, lit when on.
	const PackedStringArray names = debugger->get_category_names();
	PackedStringArray slots;
	for (int i = 0; i < names.size(); i++)
	{
		const bool on = enabled_categories.has(names[i]);
		const String slot = i < DebugTools::CATEGORY_SLOTS ? String::num_int64(i) : String("-");
		slots.push_back(String("[color=") + (on ? COLOR_GOOD : COLOR_DIM) + "]" + slot + " " + names[i] + "[/color]");
	}
	out += String("  ").join(slots) + "\n";

	for (int i = 0; i < names.size(); i++)
	{
		if (!enabled_categories.has(names[i])) { continue; }
		Ref<GameplayDebuggerCategory> category = debugger->get_category(names[i]);
		if (category.is_null()) { continue; }
		out += "\n" + head + names[i] + end_head + "\n";
		if (waiting) { continue; }
		out += category->draw_data(target, data.get(names[i], Dictionary()));
	}
	label->set_text(out);
}

// --- Console commands ---------------------------------------------------------

void GameplayDebuggerHUD::cmd_toggle(const PackedStringArray& /*args*/)
{
	set_active(!active);
}

void GameplayDebuggerHUD::cmd_category(const PackedStringArray& args)
{
	GameplayDebugger* debugger = GameplayDebugger::get_singleton();
	ConsoleManager* console = ConsoleManager::get_singleton();
	if (debugger == nullptr || console == nullptr) { return; }
	init_enabled_categories();

	const PackedStringArray names = debugger->get_category_names();
	if (args.is_empty())
	{
		for (int i = 0; i < names.size(); i++)
		{
			console->print_line(String::num_int64(i) + "  " + names[i] + (enabled_categories.has(names[i]) ? "  (on)" : ""));
		}
		return;
	}
	if (args[0].is_valid_int()) { toggle_category_slot(args[0].to_int()); }
	else { toggle_category(args[0]); }
	if (!active) { set_active(true); }
}

void GameplayDebuggerHUD::cmd_target(const PackedStringArray& args)
{
	ConsoleManager* console = ConsoleManager::get_singleton();
	const String which = args.is_empty() ? String("aim") : args[0];
	Node* node = nullptr;
	if (which == "aim")
	{
		// Resolved on the next physics step, where the ray may be cast.
		aim_lock_pending = true;
		return;
	}
	if (which == "self") { node = default_target(); }
	else if (is_inside_tree()) { node = get_tree()->get_root()->get_node_or_null(NodePath(which)); }

	if (node == nullptr)
	{
		if (console != nullptr) { console->print_error("gd.target: nothing found for \"" + which + "\"."); }
		return;
	}
	set_target(node);
	locked = true;
	if (!active) { set_active(true); }
	sync_request();
	if (console != nullptr) { console->print_line("Debugging " + String(node->get_path())); }
}

void GameplayDebuggerHUD::cmd_lock(const PackedStringArray& /*args*/)
{
	locked = !locked;
	refresh_text();
}

void GameplayDebuggerHUD::cmd_freecam(const PackedStringArray& /*args*/)
{
	set_free_camera(!is_free_camera());
}

void GameplayDebuggerHUD::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_active", "value"), &GameplayDebuggerHUD::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &GameplayDebuggerHUD::is_active);
	ClassDB::bind_method(D_METHOD("set_locked", "value"), &GameplayDebuggerHUD::set_locked);
	ClassDB::bind_method(D_METHOD("is_locked"), &GameplayDebuggerHUD::is_locked);
	ClassDB::bind_method(D_METHOD("set_target", "node"), &GameplayDebuggerHUD::set_target);
	ClassDB::bind_method(D_METHOD("get_target"), &GameplayDebuggerHUD::get_target);
	ClassDB::bind_method(D_METHOD("find_target_under_crosshair"), &GameplayDebuggerHUD::find_target_under_crosshair);
	ClassDB::bind_method(D_METHOD("set_free_camera", "value"), &GameplayDebuggerHUD::set_free_camera);
	ClassDB::bind_method(D_METHOD("is_free_camera"), &GameplayDebuggerHUD::is_free_camera);
	ClassDB::bind_method(D_METHOD("get_free_camera"), &GameplayDebuggerHUD::get_free_camera);
	ClassDB::bind_method(D_METHOD("teleport_pawn_to_free_camera"), &GameplayDebuggerHUD::teleport_pawn_to_free_camera);
	ClassDB::bind_method(D_METHOD("toggle_category", "name"), &GameplayDebuggerHUD::toggle_category);
	ClassDB::bind_method(D_METHOD("toggle_category_slot", "slot"), &GameplayDebuggerHUD::toggle_category_slot);
	ClassDB::bind_method(D_METHOD("get_enabled_categories"), &GameplayDebuggerHUD::get_enabled_categories);
	ClassDB::bind_static_method("GameplayDebuggerHUD", D_METHOD("resolve_actor", "node"), &GameplayDebuggerHUD::resolve_actor);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "locked"), "set_locked", "is_locked");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "free_camera", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_free_camera", "is_free_camera");
}
