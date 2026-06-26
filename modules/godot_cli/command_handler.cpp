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
#include "modules/godot_cli/cli_types.h"

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
#include "core/variant/variant_utility.h"
#include "core/object/script_language.h"
#include "core/os/keyboard.h"
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/control.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/main/viewport.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/animation.h"
#include "scene/resources/curve.h"
#include "scene/resources/gradient.h"

#include "core/input/input_event.h"

#include "main/performance.h"
#include "core/os/os.h"
#include "core/version.h"
#include "servers/audio/audio_server.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

#include "modules/gdscript/gdscript.h"

#include "core/input/input.h"
#include "core/input/input_map.h"
#include "scene/resources/texture.h"

// ── Log ring buffer ───────────────────────────────────────────────────
// Simple in-memory log ring buffer for debug/logs and debug/errors.

struct LogEntry {
	double time;
	String msg;
};

static Vector<LogEntry> _log_buffer;
static Vector<LogEntry> _error_buffer;
static constexpr int MAX_LOG_ENTRIES = 500;
static constexpr int MAX_ERROR_ENTRIES = 200;

static void _push_log(const String &p_msg) {
	LogEntry e;
	e.time = OS::get_singleton()->get_unix_time();
	e.msg = p_msg;
	_log_buffer.push_back(e);
	if (_log_buffer.size() > MAX_LOG_ENTRIES) {
		_log_buffer.remove_at(0);
	}
}

static void _push_error(const String &p_msg) {
	LogEntry e;
	e.time = OS::get_singleton()->get_unix_time();
	e.msg = p_msg;
	_error_buffer.push_back(e);
	if (_error_buffer.size() > MAX_ERROR_ENTRIES) {
		_error_buffer.remove_at(0);
	}
}

// ── Helper to build an Array of log entries ───────────────────────────
static Array _log_entries_to_array(const Vector<LogEntry> &p_entries, int p_count) {
	Array arr;
	int start = MAX(0, p_entries.size() - p_count);
	for (int i = start; i < p_entries.size(); i++) {
		Dictionary entry;
		entry["time"] = p_entries[i].time;
		entry["msg"] = p_entries[i].msg;
		arr.push_back(entry);
	}
	return arr;
}

// ── Helper to capture a viewport to base64 PNG ────────────────────────
static Dictionary _capture_viewport(Viewport *p_viewport) {
	Dictionary result;
	if (!p_viewport) {
		result["_ok"] = false;
		result["_error"] = "No viewport";
		return result;
	}

	Ref<ViewportTexture> tex = p_viewport->get_texture();
	if (tex.is_null()) {
		result["_ok"] = false;
		result["_error"] = "Failed to get viewport texture";
		return result;
	}

	Ref<Image> img = tex->get_image();
	if (img.is_null()) {
		result["_ok"] = false;
		result["_error"] = "Failed to get image from viewport texture (rendering may not be available in headless mode)";
		return result;
	}

	Vector<uint8_t> png_data = img->save_png_to_buffer();
	if (png_data.is_empty()) {
		result["_ok"] = false;
		result["_error"] = "Failed to encode PNG";
		return result;
	}

	String base64 = CryptoCore::b64_encode_str(png_data.ptr(), png_data.size());

	result["_ok"] = true;
	result["width"] = img->get_width();
	result["height"] = img->get_height();
	result["format"] = Image::get_format_name(img->get_format());
	result["size_bytes"] = png_data.size();
	result["data"] = base64;
	return result;
}

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
// Forward declarations for all handler functions
// (defined below but referenced by REGISTER calls)
// ========================================================================

#define HANDLER_DECL(name) static Dictionary _handler_##name(const Dictionary &p_params)
HANDLER_DECL(scene_tree);
HANDLER_DECL(scene_add_node);
HANDLER_DECL(scene_remove_node);
HANDLER_DECL(scene_get);
HANDLER_DECL(scene_set);
HANDLER_DECL(scene_save);
HANDLER_DECL(scene_open);
HANDLER_DECL(scene_new);
HANDLER_DECL(scene_attach_script);
HANDLER_DECL(scene_connect);
HANDLER_DECL(scene_validate);
HANDLER_DECL(scene_groups_add);
HANDLER_DECL(scene_groups_remove);
HANDLER_DECL(scene_groups_list);
HANDLER_DECL(game_restart);
HANDLER_DECL(project_input_bind);
HANDLER_DECL(game_run);
HANDLER_DECL(game_stop);
HANDLER_DECL(game_pause);
HANDLER_DECL(game_resume);
HANDLER_DECL(game_step);
HANDLER_DECL(input_key);
HANDLER_DECL(input_mouse_move);
HANDLER_DECL(input_mouse_button);
HANDLER_DECL(input_action);
HANDLER_DECL(debug_logs);
HANDLER_DECL(debug_errors);
HANDLER_DECL(debug_inspect);
HANDLER_DECL(debug_monitor);
HANDLER_DECL(script_write);
HANDLER_DECL(script_read);
HANDLER_DECL(script_validate);
HANDLER_DECL(render_screenshot);
HANDLER_DECL(project_settings);
HANDLER_DECL(resource_list);
HANDLER_DECL(resource_import);
HANDLER_DECL(resource_create_material);
HANDLER_DECL(resource_create_mesh);
HANDLER_DECL(resource_create_animation);
HANDLER_DECL(daemon_shutdown);
HANDLER_DECL(daemon_ping);
HANDLER_DECL(daemon_version);

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
	REGISTER("scene/tree", _handler_scene_tree);
	REGISTER("scene/add_node", _handler_scene_add_node);
	REGISTER("scene/remove_node", _handler_scene_remove_node);
	REGISTER("scene/get", _handler_scene_get);
	REGISTER("scene/set", _handler_scene_set);
	REGISTER("scene/save", _handler_scene_save);
	REGISTER("scene/open", _handler_scene_open);
	REGISTER("scene/new", _handler_scene_new);
	REGISTER("scene/attach_script", _handler_scene_attach_script);
	REGISTER("scene/connect", _handler_scene_connect);
	REGISTER("scene/validate", _handler_scene_validate);
	REGISTER("scene/groups_add", _handler_scene_groups_add);
	REGISTER("scene/groups_remove", _handler_scene_groups_remove);
	REGISTER("scene/groups_list", _handler_scene_groups_list);
	REGISTER("game/restart", _handler_game_restart);
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
		parent = static_cast<Node*>(scene_tree->get_root().ptr());
	} else {
		parent = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(parent_ref));
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
	node->set_owner(scene_tree->get_edited_scene_root() ? scene_tree->get_edited_scene_root() : static_cast<Node*>(scene_tree->get_root().ptr()));

	Dictionary result;
	result["path"] = String(node->get_path());
	result["name"] = node->get_name();
	result["type"] = node->get_class();
	return result;
}

HANDLER(scene_remove_node) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));

	String node_path = p_params.get("path", "");
	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");

	Node *node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	String parent_path = node->get_parent() ? String(node->get_parent()->get_path()) : "";
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

	Node *node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	String property = p_params.get("property", "");
	if (!property.is_empty()) {
		Dictionary result;
		Variant value = node->get(property);
		result["value"] = VariantUtilityFunctions::var_to_str(value);
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
	result["path"] = String(node->get_path());
	result["properties"] = prop_dict;
	result["child_count"] = node->get_child_count();
	result["script"] = node->get_script();

	return result;
}

HANDLER(scene_set) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));

	String node_path = p_params.get("path", "");
	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path' parameter"), "");

	Node *node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	String property = p_params.get("property", "");
	ERR_FAIL_COND_V_MSG(property.is_empty(), godot_cli::make_error(0, "Missing 'property' parameter"), "");

	Variant value = p_params.get("value", Variant());

	// Handle string paths for resource properties (e.g. shape, texture, material)
	if (value.get_type() == Variant::STRING) {
		String path_str = value;
		if (path_str.begins_with("res://") && path_str.ends_with(".tres") || path_str.ends_with(".res")) {
			Ref<Resource> loaded = ResourceLoader::load(path_str);
			if (loaded.is_valid()) {
				value = loaded;
			}
		}
	}

	// Handle Dictionary-to-Resource conversion for scene properties
	if (value.get_type() == Variant::DICTIONARY) {
		Dictionary dict = value;
		if (dict.has("resource_type")) {
			String res_type = dict["resource_type"];
			// Create resource via instantiate then wrap in Ref
			Object *obj = ClassDB::instantiate(res_type);
			if (obj) {
				Ref<Resource> res = Ref<Resource>(Object::cast_to<Resource>(obj));
				if (res.is_valid()) {
					Array keys = dict.keys();
					for (int i = 0; i < keys.size(); i++) {
						String key = keys[i];
						if (key == "resource_type") continue;
						Variant val = dict[key];
						if (val.get_type() == Variant::ARRAY) {
							Array arr = val;
							if (key == "size" && arr.size() == 2) {
								res->set(key, Vector2(arr[0], arr[1]));
							} else if (key == "size" && arr.size() == 3) {
								res->set(key, Vector3(arr[0], arr[1], arr[2]));
							} else if (key == "radius") {
								res->set(key, float(arr[0]));
							} else {
								res->set(key, val);
							}
						} else {
							res->set(key, val);
						}
					}
					// Save to temp file and reload to ensure proper resource type
					String tmp_path = vformat("res://.godot_cli_shape_%d.res", OS::get_singleton()->get_unix_time());
					ResourceSaver::save(res, tmp_path);
					value = ResourceLoader::load(tmp_path);
				} else if (obj) {
					memdelete(obj);
				}
			}
		}
	}

	// Handle array-to-Vector conversion for spatial properties
	if (value.get_type() == Variant::ARRAY) {
		Array arr = value;
		if (property == "position" || property == "scale") {
			if (arr.size() == 2) {
				value = Vector2(arr[0], arr[1]);
			} else if (arr.size() == 3) {
				value = Vector3(arr[0], arr[1], arr[2]);
			}
		} else if (property == "rotation") {
			value = float(arr[0]);
		} else if (property == "size") {
			if (arr.size() == 2) {
				value = Vector2(arr[0], arr[1]);
			}
		}
	}

	node->set(property, value);

	Dictionary result;
	result["property"] = property;
	result["value"] = VariantUtilityFunctions::var_to_str(node->get(property));
	return result;
}

HANDLER(scene_save) {
	String path = p_params.get("path", "");
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, godot_cli::make_error(0, "No scene tree available"));

	Node *root = scene_tree->get_edited_scene_root();
	if (!root) {
		root = static_cast<Node*>(scene_tree->get_root().ptr());
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

	Node *root = static_cast<Node*>(scene_tree->get_root().ptr());
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

	Node *node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
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

	Node *node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");

	Node *target = node;
	if (!target_path.is_empty()) {
		target = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(target_path));
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

HANDLER(scene_validate) {
	String path = p_params.get("path", "res://main.tscn");
	String full_path = path;
	if (!path.begins_with("res://")) {
		full_path = "res://" + path;
	}

	// Try loading as a scene - Godot reports parse errors in the process.
	Ref<PackedScene> scene = ResourceLoader::load(full_path);
	Dictionary result;
	result["path"] = full_path;
	result["valid"] = scene.is_valid();
	if (scene.is_null()) {
		result["error"] = "Scene file failed to load — likely has parse errors";
	}
	_push_log(vformat("scene/validate: %s %s", full_path, scene.is_valid() ? "valid" : "INVALID"));
	return result;
}

HANDLER(scene_groups_add) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));
	String node_path = p_params.get("path", "");
	String group = p_params.get("group", "");
	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path'"), "");
	ERR_FAIL_COND_V_MSG(group.is_empty(), godot_cli::make_error(0, "Missing 'group'"), "");
	Node *node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");
	node->add_to_group(group);
	_push_log(vformat("scene/groups_add: %s -> %s", node_path, group));
	Dictionary result;
	result["node"] = node_path;
	result["group"] = group;
	return result;
}

HANDLER(scene_groups_remove) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));
	String node_path = p_params.get("path", "");
	String group = p_params.get("group", "");
	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path'"), "");
	ERR_FAIL_COND_V_MSG(group.is_empty(), godot_cli::make_error(0, "Missing 'group'"), "");
	Node *node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");
	node->remove_from_group(group);
	Dictionary result;
	result["node"] = node_path;
	result["group"] = group;
	return result;
}

HANDLER(scene_groups_list) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, _handler_scene_tree(p_params));
	String node_path = p_params.get("path", "");
	ERR_FAIL_COND_V_MSG(node_path.is_empty(), godot_cli::make_error(0, "Missing 'path'"), "");
	Node *node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
	ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");
	List<Node::GroupInfo> ginfo;
	node->get_groups(&ginfo);
	Array groups;
	for (const Node::GroupInfo &E : ginfo) {
		groups.push_back(E.name);
	}
	Dictionary result;
	result["node"] = node_path;
	result["groups"] = groups;
	return result;
}

// ========================================================================
// Game commands
// ========================================================================

void GodotCLICommandHandler::_register_game_commands() {
	REGISTER("game/run", _handler_game_run);
	REGISTER("game/stop", _handler_game_stop);
	REGISTER("game/pause", _handler_game_pause);
	REGISTER("game/resume", _handler_game_resume);
	REGISTER("game/step", _handler_game_step);
	REGISTER("game/restart", _handler_game_restart);
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

		Node *root = static_cast<Node*>(scene_tree->get_root().ptr());
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
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree) {
		// Remove game scenes from root.
		Node *root = static_cast<Node*>(scene_tree->get_root().ptr());
		while (root->get_child_count() > 0) {
			Node *child = root->get_child(0);
			root->remove_child(child);
			child->queue_free();
		}
		scene_tree->set_pause(false);
	}
	_push_log("game/stop");
	Dictionary result;
	result["running"] = false;
	return result;
}

HANDLER(game_pause) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree) {
		scene_tree->set_pause(true);
	}
	Dictionary result;
	result["paused"] = true;
	return result;
}

HANDLER(game_resume) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree) {
		scene_tree->set_pause(false);
	}
	Dictionary result;
	result["paused"] = false;
	return result;
}

HANDLER(game_step) {
	int frames = p_params.get("frames", 1);
	_push_log(vformat("game/step: %d frames", frames));
	Dictionary result;
	result["frames"] = frames;
	return result;
}

HANDLER(game_restart) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (!scene_tree) {
		return godot_cli::make_error(0, "No scene tree available");
	}
	scene_tree->reload_current_scene();
	_push_log("game/restart");
	Dictionary result;
	result["restarted"] = true;
	return result;
}

// ========================================================================
// Input commands
// ========================================================================

void GodotCLICommandHandler::_register_input_commands() {
	REGISTER("input/key", _handler_input_key);
	REGISTER("input/mouse_move", _handler_input_mouse_move);
	REGISTER("input/mouse_button", _handler_input_mouse_button);
	REGISTER("input/action", _handler_input_action);
}

HANDLER(input_key) {
	String key_str = p_params.get("key", "");
	bool pressed = p_params.get("pressed", true);

	ERR_FAIL_COND_V_MSG(key_str.is_empty(), godot_cli::make_error(0, "Missing 'key' parameter"), "");

	Input *input = Input::get_singleton();
	ERR_FAIL_COND_V(!input, godot_cli::make_error(0, "No Input singleton available"));

	Ref<InputEventKey> key_event;
	key_event.instantiate();
	Key keycode = find_keycode(key_str);
	key_event->set_keycode(keycode);
	key_event->set_physical_keycode(keycode);
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

	MouseButton button = MouseButton::LEFT;
	if (button_str == "right") button = MouseButton::RIGHT;
	else if (button_str == "middle") button = MouseButton::MIDDLE;

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
	REGISTER("debug/logs", _handler_debug_logs);
	REGISTER("debug/errors", _handler_debug_errors);
	REGISTER("debug/inspect", _handler_debug_inspect);
	REGISTER("debug/monitor", _handler_debug_monitor);
}

HANDLER(debug_logs) {
	int count = p_params.get("count", 50);
	Dictionary result;
	result["logs"] = _log_entries_to_array(_log_buffer, count);
	result["count"] = _log_buffer.size();
	result["returned"] = MIN(count, _log_buffer.size());
	return result;
}

HANDLER(debug_errors) {
	int count = p_params.get("count", 50);
	Dictionary result;
	result["errors"] = _log_entries_to_array(_error_buffer, count);
	result["count"] = _error_buffer.size();
	result["returned"] = MIN(count, _error_buffer.size());
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
		node = static_cast<Node*>(scene_tree->get_root().ptr())->get_node(NodePath(node_path));
		ERR_FAIL_COND_V_MSG(!node, godot_cli::make_error(0, vformat("Node not found: %s", node_path)), "");
	}

	Object *target = node;
	if (!target) {
		target = static_cast<Node*>(scene_tree->get_root().ptr()); // default to root
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
			prop_info["value"] = VariantUtilityFunctions::var_to_str(target->get(E.name));
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
			child_info["path"] = String(child->get_path());
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
		result["physics_time_ms"] = perf->get_monitor(Performance::TIME_PHYSICS_PROCESS) * 1000.0;
	} else if (monitor == "memory/static") {
		result["memory_kb"] = perf->get_monitor(Performance::MEMORY_STATIC) / 1024.0;
	} else if (monitor == "objects" || monitor == "object/count") {
		result["object_count"] = perf->get_monitor(Performance::OBJECT_COUNT);
	} else if (monitor == "draw_calls" || monitor == "rendering/draw_calls") {
		result["draw_calls"] = perf->get_monitor(Performance::RENDER_TOTAL_DRAW_CALLS_IN_FRAME);
	} else {
		result["error"] = vformat("Unknown monitor: %s", monitor);
	}

	return result;
}

// ========================================================================
// Script commands
// ========================================================================

void GodotCLICommandHandler::_register_script_commands() {
	REGISTER("script/write", _handler_script_write);
	REGISTER("script/read", _handler_script_read);
	REGISTER("script/validate", _handler_script_validate);
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

	if (!source.is_empty()) {
		// Write to temp file to validate.
		String tmp_path = "res://.godot_cli_validate.gd";
		Ref<FileAccess> file = FileAccess::open(tmp_path, FileAccess::WRITE);
		if (file.is_valid()) {
			file->store_string(source);
			file->close();
		}
		path = tmp_path;
	} else if (!path.is_empty()) {
		String full_path = path;
		if (!path.begins_with("res://") && !path.begins_with("/")) {
			full_path = "res://" + path;
		}
		Ref<FileAccess> file = FileAccess::open(full_path, FileAccess::READ);
		ERR_FAIL_COND_V_MSG(file.is_null(), godot_cli::make_error(0, vformat("Failed to open file: %s", full_path)), "");
		source = file->get_as_text();
		file->close();
	}

	// Try loading as script - Godot's resource loader validates syntax.
	Array errors;
	bool valid = true;

	Ref<GDScript> script = ResourceLoader::load(path, "GDScript", ResourceLoader::CACHE_MODE_IGNORE);
	if (script.is_valid()) {
		valid = script->is_valid();
	} else {
		valid = false;
	}

	// Clean up temp file.
	if (p_params.has("source") && !p_params.has("path")) {
		DirAccess::remove_file_or_error("res://.godot_cli_validate.gd");
	}

	Dictionary result;
	result["valid"] = valid;
	result["error_count"] = errors.size();
	result["errors"] = errors;
	return result;
}

// ========================================================================
// Render commands
// ========================================================================

void GodotCLICommandHandler::_register_render_commands() {
	REGISTER("render/screenshot", _handler_render_screenshot);
}

HANDLER(render_screenshot) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	ERR_FAIL_COND_V(!scene_tree, godot_cli::make_error(0, "No scene tree available"));

	Window *root_win = scene_tree->get_root();
	Viewport *viewport = root_win;
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
	REGISTER("project/settings", _handler_project_settings);
	REGISTER("project/input_bind", _handler_project_input_bind);
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
				settings[key] = VariantUtilityFunctions::var_to_str(ps->get(key));
			}
		}
		result["settings"] = settings;
	} else if (action == "get") {
		String key = p_params.get("key", "");
		if (ps->has_setting(key)) {
			result["value"] = VariantUtilityFunctions::var_to_str(ps->get(key));
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

HANDLER(project_input_bind) {
	String action_name = p_params.get("action", "");
	Array keys = p_params.get("keys", Array());
	ERR_FAIL_COND_V_MSG(action_name.is_empty(), godot_cli::make_error(0, "Missing 'action'"), "");
	InputMap *im = InputMap::get_singleton();
	ERR_FAIL_COND_V(!im, godot_cli::make_error(0, "No InputMap"));
	if (!im->has_action(action_name)) {
		im->add_action(action_name);
	}
	for (int i = 0; i < keys.size(); i++) {
		String key_str = keys[i];
		Key keycode = find_keycode(key_str);
		Ref<InputEventKey> ie;
		ie.instantiate();
		ie->set_keycode(keycode);
		ie->set_physical_keycode(keycode);
		im->action_add_event(action_name, ie);
	}
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps) {
		ps->save();
	}
	_push_log(vformat("project/input_bind: %s = %d keys", action_name, keys.size()));
	Dictionary result;
	result["action"] = action_name;
	result["bound_keys"] = keys.size();
	return result;
}

// ========================================================================
// Resource commands
// ========================================================================

void GodotCLICommandHandler::_register_resource_commands() {
	REGISTER("resource/list", _handler_resource_list);
	REGISTER("resource/import", _handler_resource_import);
	REGISTER("resource/create_material", _handler_resource_create_material);
	REGISTER("resource/create_mesh", _handler_resource_create_mesh);
	REGISTER("resource/create_animation", _handler_resource_create_animation);
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

	_push_log(vformat("resource/import: %s -> %s", source, dest));
	Dictionary result;
	result["source"] = source;
	result["dest"] = dest;
	return result;
}

HANDLER(resource_create_material) {
	String name = p_params.get("name", "new_material");
	String material_type = p_params.get("type", "StandardMaterial3D");
	Dictionary props = p_params.get("properties", Dictionary());

	Ref<Material> material;

	if (material_type == "StandardMaterial3D" || material_type == "Material") {
		Ref<StandardMaterial3D> sm = Ref<StandardMaterial3D>(memnew(StandardMaterial3D));
		if (props.has("albedo_color")) {
			Variant c = props["albedo_color"];
			if (c.get_type() == Variant::DICTIONARY) {
				Dictionary cd = c;
				sm->set_albedo(Color(
					cd.get("r", 1.0),
					cd.get("g", 1.0),
					cd.get("b", 1.0),
					cd.get("a", 1.0)));
			} else if (c.get_type() == Variant::STRING) {
				sm->set_albedo(Color(String(c)));
			}
		}
		if (props.has("metallic")) sm->set_metallic(float(props["metallic"]));
		if (props.has("roughness")) sm->set_roughness(float(props["roughness"]));
		if (props.has("emission")) sm->set_emission(Color(props["emission"]));
		material = sm;
	}

	if (material.is_null()) {
		material = Ref<StandardMaterial3D>(memnew(StandardMaterial3D));
	}

	material->set_name(name);

	String save_path = p_params.get("save_path", "");
	if (!save_path.is_empty()) {
		Error err = ResourceSaver::save(material, save_path);
		if (err != OK) {
			_push_error(vformat("Failed to save material to %s", save_path));
		}
	}

	_push_log(vformat("resource/create_material: %s (%s)", name, material_type));
	Dictionary result;
	result["name"] = name;
	result["type"] = material_type;
	result["path"] = save_path;
	return result;
}

HANDLER(resource_create_mesh) {
	String name = p_params.get("name", "new_mesh");
	Dictionary props = p_params.get("properties", Dictionary());

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->set_name(name);

	// If vertex data provided, create surface.
	if (props.has("vertices") && props.has("indices")) {
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);

		Vector<Vector3> vertices;
		{
			Array verts = props["vertices"];
			for (int i = 0; i < verts.size(); i++) {
				Variant v = verts[i];
				if (v.get_type() == Variant::VECTOR3) {
					vertices.push_back(v);
				} else if (v.get_type() == Variant::DICTIONARY) {
					Dictionary d = v;
					vertices.push_back(Vector3(
						d.get("x", 0.0),
						d.get("y", 0.0),
						d.get("z", 0.0)));
				} else if (v.get_type() == Variant::ARRAY) {
					Array a = v;
					if (a.size() >= 3)
						vertices.push_back(Vector3(a[0], a[1], a[2]));
				}
			}
		}

		Vector<int> indices;
		{
			Array idxs = props["indices"];
			for (int i = 0; i < idxs.size(); i++) {
				indices.push_back(int(idxs[i]));
			}
		}

		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_INDEX] = indices;

		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	}

	String save_path = p_params.get("save_path", "");
	if (!save_path.is_empty()) {
		Error err = ResourceSaver::save(mesh, save_path);
		if (err != OK) {
			_push_error(vformat("Failed to save mesh to %s", save_path));
		}
	}

	_push_log(vformat("resource/create_mesh: %s", name));
	Dictionary result;
	result["name"] = name;
	result["surface_count"] = mesh->get_surface_count();
	result["path"] = save_path;
	return result;
}

HANDLER(resource_create_animation) {
	String name = p_params.get("name", "new_animation");
	Dictionary props = p_params.get("properties", Dictionary());

	Ref<Animation> anim;
	anim.instantiate();
	anim->set_name(name);

	if (props.has("length")) {
		anim->set_length(float(props["length"]));
	} else {
		anim->set_length(1.0);
	}

	if (props.has("loop_mode")) {
		String loop = props["loop_mode"];
		if (loop == "linear") anim->set_loop_mode(Animation::LOOP_LINEAR);
		else if (loop == "pingpong") anim->set_loop_mode(Animation::LOOP_PINGPONG);
		else anim->set_loop_mode(Animation::LOOP_NONE);
	}

	String save_path = p_params.get("save_path", "");
	if (!save_path.is_empty()) {
		Error err = ResourceSaver::save(anim, save_path);
		if (err != OK) {
			_push_error(vformat("Failed to save animation to %s", save_path));
		}
	}

	_push_log(vformat("resource/create_animation: %s", name));
	Dictionary result;
	result["name"] = name;
	result["length"] = anim->get_length();
	result["path"] = save_path;
	return result;
}

// ========================================================================
// Daemon commands
// ========================================================================

void GodotCLICommandHandler::_register_daemon_commands() {
	REGISTER("daemon/shutdown", _handler_daemon_shutdown);
	REGISTER("daemon/ping", _handler_daemon_ping);
	REGISTER("daemon/version", _handler_daemon_version);
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
