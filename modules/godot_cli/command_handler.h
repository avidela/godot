/**************************************************************************/
/*  command_handler.h                                                     */
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

#pragma once

#include "core/object/object.h"
#include "core/variant/dictionary.h"

typedef Dictionary (*CLICommandFunc)(const Dictionary &p_params);

/**
 * Routes incoming CLI commands to their handler implementations.
 * Each handler is a Dictionary method.
 *
 * Handlers are organized in namespaces:
 *   scene/tree       -> _handle_scene_tree
 *   scene/add_node   -> _handle_scene_add_node
 *   game/run         -> _handle_game_run
 *   input/key        -> _handle_input_key
 *   ...etc
 */
class GodotCLICommandHandler : public Object {
	GDCLASS(GodotCLICommandHandler, Object);

	static GodotCLICommandHandler *singleton;

	HashMap<String, CLICommandFunc> command_map;

	void _register_commands();
	void _register_scene_commands();
	void _register_game_commands();
	void _register_input_commands();
	void _register_debug_commands();
	void _register_script_commands();
	void _register_render_commands();
	void _register_project_commands();
	void _register_resource_commands();
	void _register_daemon_commands();

protected:
	static void _bind_methods();

public:
	static GodotCLICommandHandler *get_singleton() { return singleton; }

	Dictionary handle(const Dictionary &p_command);

	GodotCLICommandHandler();
	~GodotCLICommandHandler() override;
};
