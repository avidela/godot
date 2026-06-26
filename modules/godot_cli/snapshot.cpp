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

#include "cli_types.h"
#include "core/config/engine.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"

namespace godot_cli {

// Recursively build a snapshot of a node and its children.
static Dictionary _node_snapshot(Node *p_node, int p_max_depth = 10, int p_depth = 0, int &r_ref_counter) {
	Dictionary node_info;

	String ref = vformat("n%d", r_ref_counter++);
	node_info["ref"] = ref;
	node_info["name"] = p_node->get_name();
	node_info["type"] = p_node->get_class();
	node_info["path"] = p_node->get_path();

	// Include key properties based on type.
	Node2D *node2d = Object::cast_to<Node2D>(p_node);
	Node3D *node3d = Object::cast_to<Node3D>(p_node);
	Control *ctrl = Object::cast_to<Control>(p_node);

	if (node2d) {
		Dictionary props;
		props["position"] = VariantUtilityFunctions::str(node2d->get_position());
		props["rotation"] = node2d->get_rotation();
		props["scale"] = VariantUtilityFunctions::str(node2d->get_scale());
		props["visible"] = node2d->is_visible();
		node_info["properties"] = props;
	} else if (node3d) {
		Dictionary props;
		props["position"] = VariantUtilityFunctions::str(node3d->get_position());
		props["rotation"] = VariantUtilityFunctions::str(node3d->get_rotation());
		props["scale"] = VariantUtilityFunctions::str(node3d->get_scale());
		props["visible"] = node3d->is_visible();
		node_info["properties"] = props;
	} else if (ctrl) {
		Dictionary props;
		props["position"] = VariantUtilityFunctions::str(ctrl->get_position());
		props["size"] = VariantUtilityFunctions::str(ctrl->get_size());
		props["visible"] = ctrl->is_visible();
		node_info["properties"] = props;
	} else {
		// Generic: include position if available.
		if (p_node->has_method("get_position")) {
			Dictionary props;
			props["position"] = VariantUtilityFunctions::str(p_node->call("get_position"));
			node_info["properties"] = props;
		}
	}

	// Attached script.
	if (p_node->get_script().is_valid()) {
		Ref<Script> script_ref = p_node->get_script();
		node_info["script"] = script_ref->get_path();
	}

	// Child count.
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
		Dictionary child_info = _node_snapshot(child, 10, 0, ref_counter);
		root_nodes.push_back(child_info);
	}
	result["nodes"] = root_nodes;

	return result;
}

} // namespace godot_cli
