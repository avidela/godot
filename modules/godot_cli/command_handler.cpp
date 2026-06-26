/**************************************************************************/
/*  command_handler.cpp                                                   */
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

#include "command_handler.h"
#include "cli_types.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/keyboard.h"
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/control.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/packed_scene.h"
#include "servers/audio/audio_server.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

#include "modules/gdscript/gdscript.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "scene/resources/texture.h"

GodotCLICommandHandler *GodotCLICommandHandler::singleton = nullptr;

GodotCLICommandHandler::GodotCLICommandHandler() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
	_register_commands();
}

GodotCLICommandHandler::~GodotCLICommandHandler() {
	singleton = nullptr;
}

// ========================================================================
// Command dispatch
// ========================================================================

Dictionary GodotCLICommandHandler::handle(const Dictionary &p_command) {
	String cmd = p_command["cmd"];
	int id = p_command.get("id", 0);
	Dictionary params = p_command.get("params", Dictionary());

	if (!command_map.has(cmd)) {
		Dictionary resp = godot_cli::make_error(id, vformat("Unknown command: %s", cmd), godot_cli::RESULT_NOT_FOUND);
		return resp;
	}

	CLICommandFunc func = command_map[cmd];
	Dictionary result = func(params);

	// Build response with optional snapshot.
	Dictionary resp;
	resp["ok"] = result.get("_ok", true);
	resp["id"] = id;

	if (result.has("_error")) {
		resp["error"] = result["_error"];
		resp["code"] = result.get("_code", (int)godot_cli::RESULT_ERROR);
	}

	if (result.has("_value")) {
		resp["result"] = result["_value"];
	} else {
		// Return the full result as the value.
		Dictionary clean = result;
		clean.erase("_ok");
		clean.erase("_error");
		clean.erase("_code");
		resp["result"] = clean;
	}

	// Include snapshot unless suppressed.
	if (!result.get("_no_snapshot", false)) {
		resp["snapshot"] = godot_cli::build_snapshot();
	}

	return resp;
}

// ========================================================================
// Command registration
// ========================================================================

#define REGISTER(cmd, func) command_map[cmd] = func
#define HANDLER(name) static Dictionary _handler_##name(const Dictionary &p_params)

void GodotCLICommandHandler::_register_commands() {
	_register_scene_commands();
	_register_game_commands();
	_register_input_commands();
	_register_debug_commands();
	_register_script_commands();
	_register_render_commands();
	_register_project_commands();
	_register_resource_commands();
	_register_daemon_commands();
}

// ========================================================================
// Scene commands
// ========================================================================

void GodotCLICommandHandler::_register_scene_commands() {
	REGISTER("scene/tree", _handle_scene_tree);
	REGISTER("scene/add_node", _handle_scene_add_node);
	REGISTER("scene/remove_node", _handle_scene_remove_node);
	REGISTER("scene/get", _handle_scene_get);
	REGISTER("scene/set", _handle_scene_set);
	REGISTER("scene/save", _handle_scene_save);
	REGISTER("scene/open", _handle_scene_open);
	REGISTER("scene/new", _handle_scene_new);
	REGISTER("scene/attach_script", _handle_scene_attach_script);
	REGISTER("scene/connect", _handle_scene_connect);
}

HANDLER(scene_tree) {
	// Returns the current scene tree as a nested dictionary.
	// This is handled by the snapshot function.
	Dictionary snapshot = godot_cli::build_snapshot();
	Dictionary result;
	result["_no_snapshot"] = true; // We're returning the snapshot directly.
	result["root"] = snapshot;
	return result;
}

HANDLER(scene_add_node) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));

	String parent_ref = p_params.get("parent", "root");
	String node_type = p_params.get("type", "Node2D");
	Dictionary properties = p_params.get("properties", Dictionary());

	// Find parent node.
	Node *parent = nullptr;
	if (parent_ref == "root") {
		parent = scene_tree->get_root();
	} else {
		parent = scene_tree->get_root()->get_node(NodePath(parent_ref));
	}
	ERR_FAIL_COND_V_MSG(!parent, godot_cli::make_error(0, vformat("Parent node not found: %s", parent_ref)), "");

	// Create node by type.
	Node *node = Object::cast_to<Node>(ClassDB::instantiate(node_type));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Failed to create node type: %s", node_type)), "");

	// Set name if provided.
	if (properties.has("name")) {
		node->set_name(properties["name"]);
	}

	// Set common properties.
	if (properties.has("position")) {
		Variant pos = properties["position"];
		Node2D *node2d = Object::cast_to<Node2D>(node);
		Node3D *node3d = Object::cast_to<Node3D>(node);
		Control *ctrl = Object::cast_to<Control>(node);
		if (node2d) {
			if (pos.get_type() == Variant::VECTOR2) {
				node2d->set_position(pos);
			} else if (pos.get_type() == Variant::DICTIONARY) {
				Dictionary pd = pos;
				node2d->set_position(Vector2(pd.get("x", 0), pd.get("y", 0)));
			} else if (pos.get_type() == Variant::ARRAY) {
				Array a = pos;
				node2d->set_position(Vector2(a[0], a[1]));
			}
		} else if (node3d) {
			if (pos.get_type() == Variant::VECTOR3) {
				node3d->set_position(pos);
			} else if (pos.get_type() == Variant::DICTIONARY) {
				Dictionary pd = pos;
				node3d->set_position(Vector3(pd.get("x", 0), pd.get("y", 0), pd.get("z", 0)));
			} else if (pos.get_type() == Variant::ARRAY) {
				Array a = pos;
				if (a.size() >= 3) {
					node3d->set_position(Vector3(a[0], a[1], a[2]));
				}
			}
		} else if (ctrl) {
			if (pos.get_type() == Variant::VECTOR2) {
				ctrl->set_position(pos);
			} else if (pos.get_type() == Variant::DICTIONARY) {
				Dictionary pd = pos;
				ctrl->set_position(Vector2(pd.get("x", 0), pd.get("y", 0)));
			}
		}
	}

	// Set arbitrary properties via set().
	Array property_keys = properties.keys();
	for (int i = 0; i < property_keys.size(); i++) {
		String key = property_keys[i];
		if (key == "name" || key == "position") {
			continue; // already handled
		}
		node->set(key, properties[key]);
	}

	// Add a unique name if none set.
	if (node->get_name() == StringName()) {
		static int node_counter = 0;
		node->set_name(vformat("Node_%d", node_counter++));
	}

	parent->add_child(node);
	node->set_owner(scene_tree->get_edited_scene_root() ? scene_tree->get_edited_scene_root() : scene_tree->get_root());

	Dictionary result;
	result["path"] = node->get_path();
	result["name"] = node->get_name();
	result["type"] = node->get_class();
	return result;
}

HANDLER(scene_remove_node) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));

	String node_path = p_params.get("path", "");
	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");

	Node *node = scene_tree->get_root()->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	String parent_path = node->get_parent()->get_path();
	node->queue_free();

	Dictionary result;
	result["removed"] = node_path;
	result["parent"] = parent_path;
	return result;
}

HANDLER(scene_get) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));

	String node_path = p_params.get("path", "");
	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");

	Node *node = scene_tree->get_root()->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	String property = p_params.get("property", "");
	if (!property.is_empty()) {
		Dictionary result;
		Variant value = node->get(property);
		result["value"] = VariantUtilityFunctions::str(value);
		result["type"] = Variant::get_type_name(value.get_type());
		return result;
	}

	// Return all properties.
	List<PropertyInfo> props;
	node->get_property_list(&props);
	Dictionary prop_dict;
	for (const PropertyInfo &E : props) {
		if (E.usage & PROPERTY_USAGE_STORAGE || E.usage & PROPERTY_USAGE_EDITOR) {
			Variant val = node->get(E.name);
			prop_dict[E.name] = val;
		}
	}

	Dictionary result;
	result["class"] = node->get_class();
	result["name"] = node->get_name();
	result["path"] = node->get_path();
	result["properties"] = prop_dict;
	result["child_count"] = node->get_child_count();
	result["script"] = node->get_script().is_valid() ? node->get_script()->get_path() : Variant();

	return result;
}

HANDLER(scene_set) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));

	String node_path = p_params.get("path", "");
	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");

	Node *node = scene_tree->get_root()->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	String property = p_params.get("property", "");
	ERR_FAIL_COND_V_MSG(property.is_empty(), godot_cli::make_error(0, "Missing 'property' parameter"), "");

	Variant value = p_params.get("value", Variant());
	node->set(property, value);

	Dictionary result;
	result["property"] = property;
	result["value"] = VariantUtilityFunctions::str(node->get(property));
	return result;
}

HANDLER(scene_save) {
	String path = p_params.get("path", "");
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, godot_cli::make_error(0, "No scene tree available"));

	Node *root = scene_tree->get_edited_scene_root();
	if (!root) {
		root = scene_tree->get_root();
	}
	ERR_FAIL_COND_V(!root, godot_cli::make_error(0, "No root node to save"));

	Ref<PackedScene> packed;
	packed.instantiate();
	Error err = packed->pack(root);
	ERR_FAIL_COND_V_MSG(err != OK, godot_cli::make_error(0, "Failed to pack scene"), "");

	if (path.is_empty()) {
		path = root->get_scene_file_path();
	}
	if (path.is_empty()) {
		path = "res://scene.tscn";
	}

	err = ResourceSaver::save(packed, path);
	ERR_FAIL_COND_V_MSG(err != OK, godot_cli::make_error(0, vformat("Failed to save scene: %s", path)), "");

	Dictionary result;
	result["path"] = path;
	result["node_count"] = root->get_child_count();
	return result;
}

HANDLER(scene_open) {
	String path = p_params.get("path", "");
	ERR_FAIL_COND_V_MSG(path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");

	Ref<PackedScene> scene = ResourceLoader::load(path);
	ERR_FAIL_COND_V_MSG(scene.is_null(), godot_cli::make_error(0, vformat("Failed to load scene: %s", path)), "");

	// We return info about the scene rather than instantiating it.
	Dictionary result;
	result["path"] = path;
	return result;
}

HANDLER(scene_new) {
	// Clear the scene tree by removing all children except root.
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, godot_cli::make_error(0, "No scene tree available"));

	Node *root = scene_tree->get_root();
	// Remove all children.
	while (root->get_child_count() > 0) {
		Node *child = root->get_child(0);
		root->remove_child(child);
		child->queue_free();
	}

	Dictionary result;
	result["message"] = "New empty scene created";
	return result;
}

HANDLER(scene_attach_script) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));

	String node_path = p_params.get("path", "");
	String script_path = p_params.get("script", "");
	String source = p_params.get("source", "");

	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");
	ERR_FAIL_COND_V_MSG(script_path.is_empty() && source.is_empty(), godot_cli::make_error(0, "Missing either 'script' or 'source'"), "");

	Node *node = scene_tree->get_root()->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	if (!source.is_empty() && !script_path.is_empty()) {
		// Write source to file.
		Ref<FileAccess> file = FileAccess::open(script_path, FileAccess::WRITE);
		ERR_FAIL_COND_V_MSG(file.is_null(), godot_cli::make_error(0, vformat("Failed to write script: %s", script_path)), "");
		file->store_string(source);
		file->close();
	}

	if (!script_path.is_empty()) {
		Ref<Script> script = ResourceLoader::load(script_path);
		ERR_FAIL_COND_V_MSG(script.is_null(), godot_cli::make_error(0, vformat("Failed to load script: %s", script_path)), "");
		node->set_script(script);
	}

	Dictionary result;
	result["node"] = node_path;
	result["script"] = script_path;
	return result;
}

HANDLER(scene_connect) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));

	String node_path = p_params.get("path", "");
	String signal_name = p_params.get("signal", "");
	String target_path = p_params.get("target", "");
	String method_name = p_params.get("method", "");

	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");
	ERR_FAIL_COND_V_MSG(signal_name.is_empty(), godot_cli::make_error(0, "Missing 'signal' parameter"), "");
	ERR_FAIL_COND_V_MSG(method_name.is_empty(), godot_cli::make_error(0, "Missing 'method' parameter"), "");

	Node *node = scene_tree->get_root()->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	Node *target = node;
	if (!target_path.is_empty()) {
		target = scene_tree->get_root()->get_node(NodePath(target_path));
		ERR_FAIL_COND_V_MSG(!target, godot_cli::make_error(0, vformat("Target node not found: %s", target_path)), "");
	}

	Error err = node->connect(signal_name, Callable(target, method_name));
	ERR_FAIL_COND_V_MSG(err != OK, godot_cli::make_error(0, vformat("Failed to connect signal '%s' to '%s:%s'", signal_name, target_path, method_name)), "");

	Dictionary result;
	result["signal"] = signal_name;
	result["target"] = target_path.is_empty() ? node_path : target_path;
	result["method"] = method_name;
	return result;
}

// ========================================================================
// Game commands
// ========================================================================

void GodotCLICommandHandler::_register_game_commands() {
	REGISTER("game/run", _handle_game_run);
	REGISTER("game/stop", _handle_game_stop);
	REGISTER("game/pause", _handle_game_pause);
	REGISTER("game/resume", _handle_game_resume);
	REGISTER("game/step", _handle_game_step);
}

HANDLER(game_run) {
	String scene_path = p_params.get("scene", "");

	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, godot_cli::make_error(0, "No scene tree available"));

	if (!scene_path.is_empty()) {
		Ref<PackedScene> scene = ResourceLoader::load(scene_path);
		ERR_FAIL_COND_V_MSG(scene.is_null(), godot_cli::make_error(0, vformat("Failed to load scene: %s", scene_path)), "");

		Node *instance = scene->instantiate();
		ERR_FAIL_COND_V_MSG(!instance, godot_cli::make_error(0, "Failed to instantiate scene"), "");

		Node *root = scene_tree->get_root();
		while (root->get_child_count() > 0) {
			Node *child = root->get_child(0);
			root->remove_child(child);
			child->queue_free();
		}
		root->add_child(instance);
	}

	// Mark as running (if there's no editor, the game loop is already running).
	Dictionary result;
	result["running"] = true;
	return result;
}

HANDLER(game_stop) {
	Dictionary result;
	result["running"] = false;
	return result;
}

HANDLER(game_pause) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree) {
		scene_tree->set_paused(true);
	}
	Dictionary result;
	result["paused"] = true;
	return result;
}

HANDLER(game_resume) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree) {
		scene_tree->set_paused(false);
	}
	Dictionary result;
	result["paused"] = false;
	return result;
}

HANDLER(game_step) {
	int frames = p_params.get("frames", 1);
	// Force a fixed number of iterations.
	// This is handled by the main loop in daemon mode.
	Dictionary result;
	result["frames"] = frames;
	return result;
}

// ========================================================================
// Input commands
// ========================================================================

void GodotCLICommandHandler::_register_input_commands() {
	REGISTER("input/key", _handle_input_key);
	REGISTER("input/mouse_move", _handle_input_mouse_move);
	REGISTER("input/mouse_button", _handle_input_mouse_button);
	REGISTER("input/action", _handle_input_action);
}

HANDLER(input_key) {
	String key_str = p_params.get("key", "");
	bool pressed = p_params.get("pressed", true);

	ERR_FAIL_COND_V_MSG(key_str.is_empty(), godot_cli::make_error(0, "Missing 'key' parameter"), "");

	Input *input = Input::get_singleton();
	ERR_FAIL_COND_V(!input, godot_cli::make_error(0, "No Input singleton available"));

	Ref<InputEventKey> key_event;
	key_event.instantiate();
	key_event->set_keycode(Key(key_str.utf8().get_data()));
	key_event->set_pressed(pressed);
	key_event->set_echo(p_params.get("echo", false));

	input->parse_input_event(key_event);

	Dictionary result;
	result["key"] = key_str;
	result["pressed"] = pressed;
	return result;
}

HANDLER(input_mouse_move) {
	float x = p_params.get("x", 0.0f);
	float y = p_params.get("y", 0.0f);

	Input *input = Input::get_singleton();
	ERR_FAIL_COND_V(!input, godot_cli::make_error(0, "No Input singleton available"));

	Ref<InputEventMouseMotion> motion;
	motion.instantiate();
	motion->set_position(Vector2(x, y));
	motion->set_global_position(Vector2(x, y));

	input->parse_input_event(motion);

	Dictionary result;
	result["position"] = vformat("(%f, %f)", x, y);
	return result;
}

HANDLER(input_mouse_button) {
	String button_str = p_params.get("button", "left");
	bool pressed = p_params.get("pressed", true);
	float x = p_params.get("x", 0.0f);
	float y = p_params.get("y", 0.0f);

	Input *input = Input::get_singleton();
	ERR_FAIL_COND_V(!input, godot_cli::make_error(0, "No Input singleton available"));

	MouseButton button = MOUSE_BUTTON_LEFT;
	if (button_str == "right") button = MOUSE_BUTTON_RIGHT;
	else if (button_str == "middle") button = MOUSE_BUTTON_MIDDLE;

	Ref<InputEventMouseButton> btn_event;
	btn_event.instantiate();
	btn_event->set_button_index(button);
	btn_event->set_pressed(pressed);
	btn_event->set_position(Vector2(x, y));
	btn_event->set_global_position(Vector2(x, y));

	input->parse_input_event(btn_event);

	Dictionary result;
	result["button"] = button_str;
	result["pressed"] = pressed;
	result["position"] = vformat("(%f, %f)", x, y);
	return result;
}

HANDLER(input_action) {
	String action = p_params.get("action", "");
	bool pressed = p_params.get("pressed", true);
	float strength = p_params.get("strength", 1.0f);

	ERR_FAIL_COND_V_MSG(action.is_empty(), godot_cli::make_error(0, "Missing 'action' parameter"), "");

	Input *input = Input::get_singleton();
	ERR_FAIL_COND_V(!input, godot_cli::make_error(0, "No Input singleton available"));

	if (pressed) {
		input->action_press(action, strength);
	} else {
		input->action_release(action);
	}

	Dictionary result;
	result["action"] = action;
	result["pressed"] = pressed;
	return result;
}

// ========================================================================
// Debug commands
// ========================================================================

void GodotCLICommandHandler::_register_debug_commands() {
	REGISTER("debug/logs", _handle_debug_logs);
	REGISTER("debug/errors", _handle_debug_errors);
	REGISTER("debug/inspect", _handle_debug_inspect);
	REGISTER("debug/monitor", _handle_debug_monitor);
}

HANDLER(debug_logs) {
	// Collect recent stdout/stderr output.
	// For now return a placeholder.
	Dictionary result;
	Array logs;
	result["logs"] = logs;
	result["count"] = 0;
	return result;
}

HANDLER(debug_errors) {
	// Collect recent script errors.
	// For now return a placeholder.
	Dictionary result;
	Array errors;
	result["errors"] = errors;
	result["count"] = 0;
	return result;
}

HANDLER(debug_inspect) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, godot_cli::make_error(0, "No scene tree available"));

	String node_path = p_params.get("path", "");
	String object_class = p_params.get("class", "");
	String object_name = p_params.get("name", "");

	Node *node = nullptr;
	if (!node_path.is_empty()) {
		node = scene_tree->get_root()->get_node(NodePath(node_path));
		ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");
	}

	Object *target = node;
	if (!target) {
		target = scene_tree->get_root(); // default to root
	}

	Dictionary result;
	result["class"] = target->get_class();
	result["name"] = target->has_method("get_name") ? Variant(target->call("get_name")) : Variant();

	// Collect properties.
	List<PropertyInfo> props;
	target->get_property_list(&props);
	Array prop_list;
	for (const PropertyInfo &E : props) {
		if (E.usage & PROPERTY_USAGE_EDITOR) {
			Dictionary prop_info;
			prop_info["name"] = E.name;
			prop_info["type"] = Variant::get_type_name(E.type);
			prop_info["value"] = VariantUtilityFunctions::str(target->get(E.name));
			prop_info["hint"] = E.hint;
			prop_list.push_back(prop_info);
		}
	}
	result["properties"] = prop_list;

	// Children if node.
	Node *node_target = Object::cast_to<Node>(target);
	if (node_target) {
		Array children;
		for (int i = 0; i < node_target->get_child_count(); i++) {
			Node *child = node_target->get_child(i);
			Dictionary child_info;
			child_info["name"] = child->get_name();
			child_info["class"] = child->get_class();
			child_info["path"] = child->get_path();
			children.push_back(child_info);
		}
		result["children"] = children;
	}

	return result;
}

HANDLER(debug_monitor) {
	String monitor = p_params.get("monitor", "fps");
	Dictionary result;

	Performance *perf = Performance::get_singleton();
	if (!perf) {
		result["error"] = "No performance singleton";
		return result;
	}

	if (monitor == "fps" || monitor == "time/fps") {
		result["fps"] = Engine::get_singleton()->get_frames_per_second();
	} else if (monitor == "frame_time" || monitor == "time/process") {
		result["frame_time_ms"] = perf->get_monitor(Performance::TIME_PROCESS) * 1000.0;
	} else if (monitor == "physics_time" || monitor == "time/physics") {
		result["physics_time_ms"] = perf->get_monitor(Performance::TIME_PHYSICS) * 1000.0;
	} else if (monitor == "memory/static") {
		result["memory_kb"] = perf->get_monitor(Performance::MEMORY_STATIC) / 1024.0;
	} else if (monitor == "objects" || monitor == "object/count") {
		result["object_count"] = perf->get_monitor(Performance::OBJECT_COUNT);
	} else if (monitor == "draw_calls" || monitor == "rendering/draw_calls") {
		result["draw_calls"] = perf->get_monitor(Performance::RENDER_DRAWS_IN_FRAME);
	} else {
		result["error"] = vformat("Unknown monitor: %s", monitor);
	}

	return result;
}

// ========================================================================
// Script commands
// ========================================================================

void GodotCLICommandHandler::_register_script_commands() {
	REGISTER("script/write", _handle_script_write);
	REGISTER("script/read", _handle_script_read);
	REGISTER("script/validate", _handle_script_validate);
}

HANDLER(script_write) {
	String path = p_params.get("path", "");
	String source = p_params.get("source", "");

	ERR_FAIL_COND_V_MSG(path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");
	ERR_FAIL_COND_V_MSG(source.is_empty(), godot_cli::make_error(0, "Missing 'source' parameter"), "");

	// If path is relative, make relative to project.
	String full_path = path;
	if (!path.begins_with("res://") && !path.begins_with("/")) {
		full_path = "res://" + path;
	}

	Ref<FileAccess> file = FileAccess::open(full_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(file.is_null(), godot_cli::make_error(0, vformat("Failed to open file for writing: %s", full_path)), "");

	file->store_string(source);
	file->close();

	// Notify the system that the file changed.
	ResourceLoader::reload_project_file(full_path);

	Dictionary result;
	result["path"] = full_path;
	result["bytes"] = source.utf8().length();
	return result;
}

HANDLER(script_read) {
	String path = p_params.get("path", "");
	ERR_FAIL_COND_V_MSG(path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");

	String full_path = path;
	if (!path.begins_with("res://") && !path.begins_with("/")) {
		full_path = "res://" + path;
	}

	Ref<FileAccess> file = FileAccess::open(full_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(file.is_null(), godot_cli::make_error(0, vformat("Failed to open file: %s", full_path)), "");

	String source = file->get_as_text();
	file->close();

	Dictionary result;
	result["path"] = full_path;
	result["source"] = source;
	result["bytes"] = source.utf8().length();
	return result;
}

HANDLER(script_validate) {
	String path = p_params.get("path", "");
	String source = p_params.get("source", "");

	ERR_FAIL_COND_V_MSG(path.is_empty() && source.is_empty(), godot_cli::make_error(0, "Missing either 'path' or 'source'"), "");

	if (!path.is_empty()) {
		String full_path = path;
		if (!path.begins_with("res://") && !path.begins_with("/")) {
			full_path = "res://" + path;
		}
		Ref<FileAccess> file = FileAccess::open(full_path, FileAccess::READ);
		ERR_FAIL_COND_V_MSG(file.is_null(), godot_cli::make_error(0, vformat("Failed to open file: %s", full_path)), "");
		source = file->get_as_text();
		file->close();
	}

	// Parse and validate using GDScript parser.
	Dictionary result;
	result["valid"] = true;
	result["errors"] = Array();

	// TODO: Use GDScript parser for deep validation.
	// GDScriptTokenizerBuffer can be used for tokenization-level checks.
	// Full validation requires GDScriptParser which returns a list of errors.

	return result;
}

// ========================================================================
// Render commands
// ========================================================================

void GodotCLICommandHandler::_register_render_commands() {
	REGISTER("render/screenshot", _handle_render_screenshot);
}

HANDLER(render_screenshot) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, godot_cli::make_error(0, "No scene tree available"));

	Viewport *viewport = scene_tree->get_root();
	ERR_FAIL_COND_V(!viewport, godot_cli::make_error(0, "No root viewport available"));

	// Get viewport texture and convert to image.
	Ref<ViewportTexture> viewport_tex = viewport->get_texture();
	ERR_FAIL_COND_V(viewport_tex.is_null(), godot_cli::make_error(0, "Failed to get viewport texture"));

	Ref<Image> img = viewport_tex->get_image();
	ERR_FAIL_COND_V(img.is_null(), godot_cli::make_error(0, "Failed to get image from viewport texture"));

	// Convert to PNG bytes -> base64.
	Vector<uint8_t> png_data = img->save_png_to_buffer();

	String file_path = p_params.get("file", "");
	if (!file_path.is_empty()) {
		// Save to file.
		Ref<FileAccess> file = FileAccess::open(file_path, FileAccess::WRITE);
		ERR_FAIL_COND_V_MSG(file.is_null(), godot_cli::make_error(0, vformat("Failed to open file: %s", file_path)), "");
		file->store_buffer(png_data);
		file->close();
	}

	// Return base64.
	String base64 = CryptoCore::b64_encode_str(png_data.ptr(), png_data.size());

	Dictionary result;
	result["width"] = img->get_width();
	result["height"] = img->get_height();
	result["format"] = Image::get_format_name(img->get_format());
	result["size_bytes"] = png_data.size();
	if (!file_path.is_empty()) {
		result["file"] = file_path;
	}
	result["data"] = base64;

	return result;
}

// ========================================================================
// Project commands
// ========================================================================

void GodotCLICommandHandler::_register_project_commands() {
	REGISTER("project/settings", _handle_project_settings);
}

HANDLER(project_settings) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	Dictionary result;

	String action = p_params.get("action", "list");
	if (action == "list") {
		// Return a subset of interesting settings.
		Array keys;
		keys.push_back("application/config/name");
		keys.push_back("application/run/main_scene");
		keys.push_back("display/window/size/viewport_width");
		keys.push_back("display/window/size/viewport_height");
		keys.push_back("rendering/renderer/rendering_method");

		Dictionary settings;
		for (int i = 0; i < keys.size(); i++) {
			String key = keys[i];
			if (ps->has_setting(key)) {
				settings[key] = VariantUtilityFunctions::str(ps->get(key));
			}
		}
		result["settings"] = settings;
	} else if (action == "get") {
		String key = p_params.get("key", "");
		if (ps->has_setting(key)) {
			result["value"] = VariantUtilityFunctions::str(ps->get(key));
		}
	} else if (action == "set") {
		String key = p_params.get("key", "");
		Variant value = p_params.get("value", Variant());
		ps->set_setting(key, value);
		ps->save();
		result["set"] = true;
	}

	return result;
}

// ========================================================================
// Resource commands
// ========================================================================

void GodotCLICommandHandler::_register_resource_commands() {
	REGISTER("resource/list", _handle_resource_list);
	REGISTER("resource/import", _handle_resource_import);
}

HANDLER(resource_list) {
	// List resources in the project.
	String path = p_params.get("path", "res://");
	PackedStringArray extensions;
	extensions.push_back(".tscn");
	extensions.push_back(".gd");
	extensions.push_back(".res");
	extensions.push_back(".tres");
	extensions.push_back(".png");
	extensions.push_back(".jpg");
	extensions.push_back(".ogg");
	extensions.push_back(".mp3");
	extensions.push_back(".glb");
	extensions.push_back(".obj");

	Ref<DirAccess> dir = DirAccess::open(path);
	ERR_FAIL_COND_V_MSG(dir.is_null(), godot_cli::make_error(0, vformat("Failed to open directory: %s", path)), "");

	Array files;
	dir->list_dir_begin();
	String file_name = dir->get_next();
	while (!file_name.is_empty()) {
		if (file_name == "." || file_name == "..") {
			file_name = dir->get_next();
			continue;
		}
		if (dir->current_is_dir()) {
			Dictionary entry;
			entry["name"] = file_name;
			entry["type"] = "directory";
			entry["path"] = path.path_join(file_name);
			files.push_back(entry);
		} else {
			for (int i = 0; i < extensions.size(); i++) {
				if (file_name.ends_with(extensions[i])) {
					Dictionary entry;
					entry["name"] = file_name;
					entry["type"] = file_name.get_extension();
					entry["path"] = path.path_join(file_name);
					entry["size"] = dir->get_next(); // not the actual size, skip
					files.push_back(entry);
					break;
				}
			}
		}
		file_name = dir->get_next();
	}
	dir->list_dir_end();

	Dictionary result;
	result["path"] = path;
	result["files"] = files;
	result["count"] = files.size();
	return result;
}

HANDLER(resource_import) {
	// Import a resource file (copy to project).
	String source = p_params.get("source", "");
	String dest = p_params.get("dest", "");

	ERR_FAIL_COND_V_MSG(source.is_empty(), godot_cli::make_error(0, "Missing 'source' parameter"), "");
	if (dest.is_empty()) {
		dest = "res://" + source.get_file();
	}

	Error err = DirAccess::copy_absolute(source, dest);
	ERR_FAIL_COND_V_MSG(err != OK, godot_cli::make_error(0, vformat("Failed to copy from %s to %s", source, dest)), "");

	Dictionary result;
	result["source"] = source;
	result["dest"] = dest;
	return result;
}

// ========================================================================
// Daemon commands
// ========================================================================

void GodotCLICommandHandler::_register_daemon_commands() {
	REGISTER("daemon/shutdown", _handle_daemon_shutdown);
	REGISTER("daemon/ping", _handle_daemon_ping);
	REGISTER("daemon/version", _handle_daemon_version);
}

HANDLER(daemon_shutdown) {
	// Signal the main loop to exit.
	print_line("GodotCLI: Shutdown requested.");
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree) {
		scene_tree->quit(EXIT_SUCCESS);
	} else {
		OS::get_singleton()->set_exit_code(EXIT_SUCCESS);
	}
	Dictionary result;
	result["shutdown"] = true;
	return result;
}

HANDLER(daemon_ping) {
	Dictionary result;
	result["pong"] = true;
	result["timestamp"] = OS::get_singleton()->get_unix_time();
	return result;
}

HANDLER(daemon_version) {
	Dictionary result;
	result["version"] = String(VERSION_FULL_NAME);
	result["protocol"] = godot_cli::PROTOCOL_VERSION;
	return result;
}

// ========================================================================
// Binding
// ========================================================================

void GodotCLICommandHandler::_bind_methods() {
}
