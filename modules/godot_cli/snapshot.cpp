/**************************************************************************/
/*  snapshot.cpp                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT CLI MODULE                           */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026 Andres Videla. MIT License.                         */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "modules/godot_cli/cli_types.h"
#include "core/config/engine.h"
#include "core/object/script_language.h"
#include "core/variant/variant_utility.h"
#include "scene/2d/node_2d.h"
#include "scene/gui/control.h"
#include "scene/main/window.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/resources/2d/shape_2d.h"
#include "scene/gui/label.h"
#include "scene/2d/sprite_2d.h"
#include "scene/2d/physics/character_body_2d.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "scene/resources/2d/circle_shape_2d.h"
#include "scene/gui/color_rect.h"

#ifndef _3D_DISABLED
#include "scene/3d/node_3d.h"
#endif
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"

namespace godot_cli {

// Helper: get exported script properties (members with @export)
static void _add_script_properties(Node *p_node, Dictionary &r_props) {
	Variant script_var = p_node->get_script();
	Object *script_obj = script_var;
	Script *script = Object::cast_to<Script>(script_obj);
	if (!script) return;

	List<PropertyInfo> pinfo;
	script->get_script_property_list(&pinfo);
	Array script_props;
	for (const PropertyInfo &E : pinfo) {
		if (E.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) {
			Dictionary pd;
			pd["name"] = E.name;
			pd["type"] = Variant::get_type_name(E.type);
			pd["value"] = VariantUtilityFunctions::var_to_str(p_node->get(E.name));
			script_props.push_back(pd);
		}
	}
	if (script_props.size() > 0) {
		r_props["script_vars"] = script_props;
	}
}

// Helper: get node groups
static Array _get_node_groups(Node *p_node) {
	Array groups;
	List<Node::GroupInfo> ginfo;
	p_node->get_groups(&ginfo);
	for (const Node::GroupInfo &E : ginfo) {
		groups.push_back(E.name);
	}
	return groups;
}

// Helper: get signal connections for a node
static Array _get_node_signals(Node *p_node) {
	Array sigs;
	List<Object::Connection> connections;
	p_node->get_all_signal_connections(&connections);
	for (const Object::Connection &E : connections) {
		Dictionary cd;
		cd["signal"] = E.signal.get_name();
		cd["target"] = E.callable.get_object() ? E.callable.get_object()->to_string() : "";
		cd["method"] = E.callable.get_method();
		sigs.push_back(cd);
	}
	return sigs;
}

// Recursively build a snapshot of a node and its children.
static Dictionary _node_snapshot(Node *p_node, int p_max_depth, int p_depth, int &r_ref_counter) {
	Dictionary node_info;

	String ref = vformat("n%d", r_ref_counter++);
	node_info["ref"] = ref;
	node_info["name"] = p_node->get_name();
	node_info["type"] = p_node->get_class();
	node_info["path"] = p_node->get_path();

	// Groups.
	Array groups = _get_node_groups(p_node);
	if (groups.size() > 0) {
		node_info["groups"] = groups;
	}

	// Signal connections.
	Array signals = _get_node_signals(p_node);
	if (signals.size() > 0) {
		node_info["signals"] = signals;
	}

	// Rich properties based on concrete type.
	Dictionary props;

	Node2D *node2d = Object::cast_to<Node2D>(p_node);
#ifndef _3D_DISABLED
	Node3D *node3d = Object::cast_to<Node3D>(p_node);
#endif
	Control *ctrl = Object::cast_to<Control>(p_node);

	// Always include visibility and position.
	if (node2d || ctrl) {
		props["visible"] = Object::cast_to<CanvasItem>(p_node)->is_visible_in_tree();
	}

	if (node2d) {
		props["position"] = VariantUtilityFunctions::var_to_str(node2d->get_position());
		props["global_position"] = VariantUtilityFunctions::var_to_str(node2d->get_global_position());
		props["rotation"] = node2d->get_rotation();
		props["scale"] = VariantUtilityFunctions::var_to_str(node2d->get_scale());
		props["z_index"] = node2d->get_z_index();
#ifndef _3D_DISABLED
	} else if (node3d) {
		props["position"] = VariantUtilityFunctions::var_to_str(node3d->get_position());
		props["global_position"] = VariantUtilityFunctions::var_to_str(node3d->get_global_position());
		props["rotation"] = VariantUtilityFunctions::var_to_str(node3d->get_rotation());
		props["scale"] = VariantUtilityFunctions::var_to_str(node3d->get_scale());
#endif
	} else if (ctrl) {
		props["position"] = VariantUtilityFunctions::var_to_str(ctrl->get_position());
		props["size"] = VariantUtilityFunctions::var_to_str(ctrl->get_size());
		props["global_position"] = VariantUtilityFunctions::var_to_str(ctrl->get_global_position());
		props["rect_size"] = VariantUtilityFunctions::var_to_str(ctrl->get_rect().size);

		// Label-specific: text content
		Label *label = Object::cast_to<Label>(p_node);
		if (label) {
			props["text"] = label->get_text();
			props["font_size"] = label->get_theme_font_size("font_size");
		}
	}

	// Sprite2D: texture path
	Sprite2D *sprite = Object::cast_to<Sprite2D>(p_node);
	if (sprite) {
		Ref<Texture2D> tex = sprite->get_texture();
		if (tex.is_valid()) {
			props["texture"] = tex->get_path();
		}
		props["centered"] = sprite->is_centered();
	}

	// CollisionShape2D: shape details
	CollisionShape2D *col_shape = Object::cast_to<CollisionShape2D>(p_node);
	if (col_shape) {
		Ref<Shape2D> shape = col_shape->get_shape();
		if (shape.is_valid()) {
			Dictionary shape_info;
			shape_info["type"] = shape->get_class();
			// Get shape-specific properties
			RectangleShape2D *rect = Object::cast_to<RectangleShape2D>(shape.ptr());
			if (rect) {
				shape_info["size"] = VariantUtilityFunctions::var_to_str(rect->get_size());
			}
			CircleShape2D *circle = Object::cast_to<CircleShape2D>(shape.ptr());
			if (circle) {
				shape_info["radius"] = circle->get_radius();
			}
			props["collision_shape"] = shape_info;
		}
		props["disabled"] = col_shape->is_disabled();
	}

	// ColorRect: color property
	ColorRect *color_rect = Object::cast_to<ColorRect>(p_node);
	if (color_rect) {
		props["color"] = VariantUtilityFunctions::var_to_str(color_rect->get_color());
		props["size"] = VariantUtilityFunctions::var_to_str(color_rect->get_size());
	}

	// CharacterBody2D: physics state
	CharacterBody2D *char_body = Object::cast_to<CharacterBody2D>(p_node);
	if (char_body) {
		props["velocity"] = VariantUtilityFunctions::var_to_str(char_body->get_velocity());
		props["on_floor"] = char_body->is_on_floor();
		props["on_wall"] = char_body->is_on_wall();
		props["floor_normal"] = VariantUtilityFunctions::var_to_str(char_body->get_floor_normal());
	}

	if (props.size() > 0) {
		node_info["properties"] = props;
	}

	// Attached script.
	{
		Variant script_var = p_node->get_script();
		Object *script_obj = script_var;
		Script *script = Object::cast_to<Script>(script_obj);
		if (script) {
			node_info["script"] = script->get_path();
		}
	}

	// Script exported variables.
	Dictionary extra_props;
	_add_script_properties(p_node, extra_props);
	if (extra_props.size() > 0) {
		node_info["script_vars"] = extra_props["script_vars"];
	}

	// Editor description.
	String desc = p_node->get_editor_description();
	if (!desc.is_empty()) {
		node_info["description"] = desc;
	}

	// Children.
	int child_count = p_node->get_child_count();
	if (child_count > 0) {
		node_info["child_count"] = child_count;
		if (p_depth < p_max_depth) {
			Array children;
			for (int i = 0; i < child_count; i++) {
				Node *child = p_node->get_child(i);
				Dictionary child_info = _node_snapshot(child, p_max_depth, p_depth + 1, r_ref_counter);
				children.push_back(child_info);
			}
			node_info["children"] = children;
		}
	}

	return node_info;
}

Dictionary build_snapshot() {
	SceneTree *scene_tree = SceneTree::get_singleton();
	Dictionary result;

	if (!scene_tree) {
		result["nodes"] = Array();
		result["running"] = false;
		return result;
	}

	Node *root = scene_tree->get_root();
	if (!root) {
		result["nodes"] = Array();
		result["running"] = false;
		return result;
	}

	result["running"] = Engine::get_singleton()->is_editor_hint() ? false : true;
	result["is_editor"] = Engine::get_singleton()->is_editor_hint();
	result["paused"] = scene_tree->is_paused();
	result["fps"] = Engine::get_singleton()->get_frames_per_second();
	result["node_count"] = root->get_child_count();

	int ref_counter = 0;
	Array root_nodes;
	for (int i = 0; i < root->get_child_count(); i++) {
		Node *child = root->get_child(i);
		Dictionary child_info = _node_snapshot(child, 8, 0, ref_counter);
		root_nodes.push_back(child_info);
	}
	result["nodes"] = root_nodes;

	return result;
}

} // namespace godot_cli
