#include "debug/debug_draw.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "debug/debug_tools.h"

using namespace GFGD;

DebugDraw* DebugDraw::singleton = nullptr;

namespace
{
constexpr int SPHERE_SEGMENTS = 16;
constexpr int MAX_LINES = 20000;
}

DebugDraw::DebugDraw()
{
	enabled = true;
	node_pending = false;
}

void DebugDraw::create_singleton()
{
	if (singleton != nullptr) { return; }
	singleton = memnew(DebugDraw);
	Engine::get_singleton()->register_singleton("DebugDraw", singleton);
}

void DebugDraw::destroy_singleton()
{
	if (singleton == nullptr) { return; }
	Engine::get_singleton()->unregister_singleton("DebugDraw");
	memdelete(singleton);
	singleton = nullptr;
}

bool DebugDraw::accepting() const
{
	return enabled && !Engine::get_singleton()->is_editor_hint() && DebugTools::is_enabled();
}

double DebugDraw::now() const
{
	return Time::get_singleton()->get_ticks_msec() / 1000.0;
}

void DebugDraw::ensure_node()
{
	if (node_pending || ObjectDB::get_instance(node_id) != nullptr) { return; }
	SceneTree* tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (tree == nullptr || tree->get_root() == nullptr) { return; }

	DebugDrawNode* node = memnew(DebugDrawNode);
	node->set_name("GFGDDebugDraw");
	node_id = node->get_instance_id();
	node_pending = true;
	// Deferred: the first call may come from inside a _ready, while the root is
	// busy adding children.
	tree->get_root()->call_deferred("add_child", node);
}

void DebugDraw::node_ready(DebugDrawNode* /*node*/)
{
	node_pending = false;
}

void DebugDraw::add_line(const Vector3& from, const Vector3& to, const Color& color, double duration)
{
	if (lines.size() >= MAX_LINES) { return; }
	Line l;
	l.from = from;
	l.to = to;
	l.color = color;
	l.single_frame = duration <= 0.0;
	l.expires = now() + duration;
	lines.push_back(l);
}

void DebugDraw::line(const Vector3& from, const Vector3& to, const Color& color, double duration)
{
	if (!accepting()) { return; }
	ensure_node();
	add_line(from, to, color, duration);
}

void DebugDraw::arrow(const Vector3& from, const Vector3& to, const Color& color, double duration)
{
	if (!accepting()) { return; }
	ensure_node();
	add_line(from, to, color, duration);

	const Vector3 direction = to - from;
	const float length = direction.length();
	if (length < 0.0001f) { return; }
	const Vector3 forward = direction / length;
	const Vector3 up = Math::abs(forward.dot(Vector3(0, 1, 0))) > 0.95f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
	const Vector3 side = forward.cross(up).normalized();
	const Vector3 side2 = forward.cross(side).normalized();
	const float head = MIN(0.25f, length * 0.3f);
	add_line(to, to - forward * head + side * head * 0.5f, color, duration);
	add_line(to, to - forward * head - side * head * 0.5f, color, duration);
	add_line(to, to - forward * head + side2 * head * 0.5f, color, duration);
	add_line(to, to - forward * head - side2 * head * 0.5f, color, duration);
}

void DebugDraw::sphere(const Vector3& center, float radius, const Color& color, double duration)
{
	if (!accepting()) { return; }
	ensure_node();
	// Three great circles.
	for (int axis = 0; axis < 3; axis++)
	{
		for (int i = 0; i < SPHERE_SEGMENTS; i++)
		{
			const float a0 = Math::TAU * i / SPHERE_SEGMENTS;
			const float a1 = Math::TAU * (i + 1) / SPHERE_SEGMENTS;
			Vector3 p0, p1;
			switch (axis)
			{
				case 0: p0 = Vector3(Math::cos(a0), Math::sin(a0), 0); p1 = Vector3(Math::cos(a1), Math::sin(a1), 0); break;
				case 1: p0 = Vector3(Math::cos(a0), 0, Math::sin(a0)); p1 = Vector3(Math::cos(a1), 0, Math::sin(a1)); break;
				default: p0 = Vector3(0, Math::cos(a0), Math::sin(a0)); p1 = Vector3(0, Math::cos(a1), Math::sin(a1)); break;
			}
			add_line(center + p0 * radius, center + p1 * radius, color, duration);
		}
	}
}

void DebugDraw::box(const Vector3& center, const Vector3& size, const Color& color, double duration)
{
	transformed_box(Transform3D(Basis(), center), size, color, duration);
}

void DebugDraw::transformed_box(const Transform3D& transform, const Vector3& size, const Color& color, double duration)
{
	if (!accepting()) { return; }
	ensure_node();
	const Vector3 h = size * 0.5;
	Vector3 corners[8];
	for (int i = 0; i < 8; i++)
	{
		corners[i] = transform.xform(Vector3(i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z));
	}
	const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	for (const auto& edge : edges) { add_line(corners[edge[0]], corners[edge[1]], color, duration); }
}

void DebugDraw::point(const Vector3& position, float size, const Color& color, double duration)
{
	if (!accepting()) { return; }
	ensure_node();
	const float h = size * 0.5f;
	add_line(position - Vector3(h, 0, 0), position + Vector3(h, 0, 0), color, duration);
	add_line(position - Vector3(0, h, 0), position + Vector3(0, h, 0), color, duration);
	add_line(position - Vector3(0, 0, h), position + Vector3(0, 0, h), color, duration);
}

void DebugDraw::axes(const Transform3D& transform, float size, double duration)
{
	arrow(transform.origin, transform.origin + transform.basis.get_column(0).normalized() * size, Color(1, 0.3, 0.3), duration);
	arrow(transform.origin, transform.origin + transform.basis.get_column(1).normalized() * size, Color(0.3, 1, 0.3), duration);
	arrow(transform.origin, transform.origin + transform.basis.get_column(2).normalized() * size, Color(0.3, 0.5, 1), duration);
}

void DebugDraw::text(const Vector3& position, const String& text, const Color& color, double duration)
{
	if (!accepting()) { return; }
	ensure_node();
	Text t;
	t.position = position;
	t.text = text;
	t.color = color;
	t.single_frame = duration <= 0.0;
	t.expires = now() + duration;
	texts.push_back(t);
}

void DebugDraw::clear()
{
	lines.clear();
	texts.clear();
}

void DebugDraw::set_enabled(bool value)
{
	enabled = value;
	if (!value) { clear(); }
}

void DebugDraw::end_frame()
{
	const double time = now();
	Vector<Line> kept_lines;
	for (const Line& l : lines)
	{
		if (!l.single_frame && l.expires > time) { kept_lines.push_back(l); }
	}
	lines = kept_lines;

	Vector<Text> kept_texts;
	for (const Text& t : texts)
	{
		if (!t.single_frame && t.expires > time) { kept_texts.push_back(t); }
	}
	texts = kept_texts;
}

void DebugDraw::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("line", "from", "to", "color", "duration"), &DebugDraw::line, DEFVAL(Color(1, 0, 0)), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("arrow", "from", "to", "color", "duration"), &DebugDraw::arrow, DEFVAL(Color(1, 0, 0)), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("sphere", "center", "radius", "color", "duration"), &DebugDraw::sphere, DEFVAL(Color(1, 0, 0)), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("box", "center", "size", "color", "duration"), &DebugDraw::box, DEFVAL(Color(1, 0, 0)), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("transformed_box", "transform", "size", "color", "duration"), &DebugDraw::transformed_box, DEFVAL(Color(1, 0, 0)), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("point", "position", "size", "color", "duration"), &DebugDraw::point, DEFVAL(0.1f), DEFVAL(Color(1, 0, 0)), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("axes", "transform", "size", "duration"), &DebugDraw::axes, DEFVAL(0.5f), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("text", "position", "text", "color", "duration"), &DebugDraw::text, DEFVAL(Color(1, 1, 1)), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("clear"), &DebugDraw::clear);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &DebugDraw::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &DebugDraw::is_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
}

// --- Node ---------------------------------------------------------------------

DebugDrawNode::DebugDrawNode()
{
	canvas = nullptr;
}

void DebugDrawNode::_ready()
{
	if (DebugDraw* draw = DebugDraw::get_singleton()) { draw->node_ready(this); }

	// Last in the frame, so whatever was drawn during this frame's _process
	// calls makes it in.
	set_process_priority(1000000);
	set_process_mode(PROCESS_MODE_ALWAYS);

	mesh.instantiate();
	MeshInstance3D* instance = memnew(MeshInstance3D);
	instance->set_name("Lines");
	instance->set_mesh(mesh);
	instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material->set_render_priority(127);
	instance->set_material_override(material);
	// Big bounds so it is never culled; the mesh changes every frame anyway.
	instance->set_custom_aabb(AABB(Vector3(-1e5, -1e5, -1e5), Vector3(2e5, 2e5, 2e5)));
	add_child(instance);

	CanvasLayer* layer = memnew(CanvasLayer);
	layer->set_name("Text");
	layer->set_layer(119);
	add_child(layer);
	canvas = memnew(DebugDrawCanvas);
	canvas->set_anchors_preset(Control::PRESET_FULL_RECT);
	canvas->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	layer->add_child(canvas);
}

void DebugDrawNode::_process(double /*delta*/)
{
	DebugDraw* draw = DebugDraw::get_singleton();
	if (draw == nullptr || mesh.is_null()) { return; }

	mesh->clear_surfaces();
	const Vector<DebugDraw::Line>& lines = draw->get_lines();
	if (!lines.is_empty())
	{
		mesh->surface_begin(Mesh::PRIMITIVE_LINES);
		for (const DebugDraw::Line& l : lines)
		{
			mesh->surface_set_color(l.color);
			mesh->surface_add_vertex(l.from);
			mesh->surface_set_color(l.color);
			mesh->surface_add_vertex(l.to);
		}
		mesh->surface_end();
	}

	frame_texts = draw->get_texts();
	if (canvas != nullptr) { canvas->queue_redraw(); }
	draw->end_frame();
}

void DebugDrawCanvas::_draw()
{
	DebugDrawNode* node = Object::cast_to<DebugDrawNode>(get_parent() != nullptr ? get_parent()->get_parent() : nullptr);
	Viewport* viewport = get_viewport();
	Camera3D* camera = viewport != nullptr ? viewport->get_camera_3d() : nullptr;
	if (node == nullptr || camera == nullptr) { return; }

	Ref<Font> font = get_theme_default_font();
	if (font.is_null()) { return; }
	const int size = 14;
	for (const DebugDraw::Text& t : node->get_frame_texts())
	{
		if (camera->is_position_behind(t.position)) { continue; }
		const Vector2 screen = camera->unproject_position(t.position);
		const float width = font->get_string_size(t.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x;
		const Vector2 at = screen - Vector2(width * 0.5f, 0);
		draw_string_outline(font, at, t.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, 4, Color(0, 0, 0, 0.85));
		draw_string(font, at, t.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, t.color);
	}
}
