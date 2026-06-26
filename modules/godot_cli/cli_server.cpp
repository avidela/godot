/**************************************************************************/
/*  cli_server.cpp                                                        */
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

#include "cli_server.h"
#include "modules/godot_cli/cli_types.h"
#include "command_handler.h"

#include "core/io/ip.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"

GodotCLIServer *GodotCLIServer::singleton = nullptr;

GodotCLIServer::GodotCLIServer() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
	handler.instantiate();
}

GodotCLIServer::~GodotCLIServer() {
	stop();
	singleton = nullptr;
}

Error GodotCLIServer::start(int p_port) {
	ERR_FAIL_COND_V_MSG(running, ERR_ALREADY_IN_USE, "GodotCLIServer is already running.");

	listen_port = p_port;
	server.instantiate();

	IPAddress bind_ip = IPAddress("127.0.0.1");
	Error err = server->listen(listen_port, bind_ip);
	if (err != OK) {
		ERR_PRINT(vformat("GodotCLIServer: Failed to listen on 127.0.0.1:%d", listen_port));
		return err;
	}

	running = true;
	print_line(vformat("GodotCLIServer: Listening on 127.0.0.1:%d", listen_port));
	return OK;
}

void GodotCLIServer::stop() {
	if (!running) {
		return;
	}

	running = false;

	if (current_client.is_valid() && current_client->connection.is_valid()) {
		current_client->connection->disconnect_from_host();
		current_client.unref();
	}

	if (server.is_valid()) {
		server->stop();
		server.unref();
	}

	print_line("GodotCLIServer: Stopped.");
}

void GodotCLIServer::poll() {
	if (!running || server.is_null()) {
		return;
	}

	// Accept new connections (we only handle one at a time for simplicity).
	if (current_client.is_null() && server->is_connection_available()) {
		_on_client_connected();
	}

	// Handle data from the current client.
	if (current_client.is_valid()) {
		Error err = current_client->handle_data();
		if (err != OK && err != ERR_BUSY) {
			_on_client_disconnected();
			return;
		}

		err = current_client->send_data();
		if (err != OK && err != ERR_BUSY) {
			_on_client_disconnected();
			return;
		}
	}
}

Error GodotCLIServer::_on_client_connected() {
	ERR_FAIL_COND_V(server.is_null(), ERR_UNCONFIGURED);

	Ref<StreamPeerTCP> connection = server->take_connection();
	ERR_FAIL_COND_V(connection.is_null(), ERR_BUG);

	current_client.instantiate();
	current_client->connection = connection;
	print_line("GodotCLIServer: Client connected.");
	return OK;
}

void GodotCLIServer::_on_client_disconnected() {
	if (current_client.is_valid()) {
		if (current_client->connection.is_valid()) {
			current_client->connection->disconnect_from_host();
		}
		current_client.unref();
		print_line("GodotCLIServer: Client disconnected.");
	}
}

String GodotCLIServer::_process_message(const String &p_message) {
	Dictionary cmd = _parse_command(p_message);
	if (cmd.is_empty()) {
		Dictionary err_resp = godot_cli::make_error(0, "Invalid JSON command", godot_cli::RESULT_INVALID_PARAMS);
		return JSON::stringify(err_resp);
	}

	// Route to command handler.
	Dictionary response = handler->handle(cmd);
	return JSON::stringify(response);
}

Dictionary GodotCLIServer::_parse_command(const String &p_message) {
	// Expect: {"cmd":"scene/tree","params":{...},"id":1}
	// Or:    {"cmd":"..."}
	Dictionary cmd;
	Variant parsed;
	{
		Variant v = JSON::parse_string(p_message);
		if (v.get_type() != Variant::DICTIONARY) {
			return cmd;
		}
		cmd = v;
	}

	if (!cmd.has("cmd")) {
		return Dictionary();
	}

	if (!cmd.has("id")) {
		cmd["id"] = 0;
	}

	return cmd;
}

// ---- CLIRequest implementation ----
// Pattern follows GDScriptLanguageProtocol::LSPeer::handle_data

Error GodotCLIServer::CLIRequest::handle_data() {
	if (connection.is_null()) {
		return ERR_CONNECTION_ERROR;
	}

	int read = 0;

	// Read headers.
	if (!has_header) {
		while (true) {
			if (buffer_pos >= godot_cli::MAX_MESSAGE_SIZE) {
				buffer_pos = 0;
				ERR_FAIL_V_MSG(ERR_OUT_OF_MEMORY, "GodotCLI: Request header too big");
			}
			Error err = connection->get_partial_data(&buffer[buffer_pos], 1, read);
			if (err != OK) {
				return err;
			} else if (read != 1) {
				return ERR_BUSY; // No data yet, try again next poll.
			}

			char *r = (char *)buffer;
			int l = buffer_pos;

			// End of headers: \r\n\r\n
			if (l > 3 && r[l] == '\n' && r[l - 1] == '\r' && r[l - 2] == '\n' && r[l - 3] == '\r') {
				r[l - 3] = '\0';
				String header = String::utf8(r);
				content_length = header.substr(16).to_int();
				has_header = true;
				buffer_pos = 0;
				break;
			}
			buffer_pos++;
		}
	}

	// Read body.
	if (has_header && !has_content) {
		while (buffer_pos < content_length) {
			if (buffer_pos >= godot_cli::MAX_MESSAGE_SIZE) {
				buffer_pos = 0;
				has_header = false;
				ERR_FAIL_COND_V_MSG(buffer_pos >= godot_cli::MAX_MESSAGE_SIZE, ERR_OUT_OF_MEMORY, "GodotCLI: Request content too big");
			}
			Error err = connection->get_partial_data(&buffer[buffer_pos], 1, read);
			if (err != OK) {
				return err;
			} else if (read != 1) {
				return ERR_BUSY;
			}
			buffer_pos++;
		}

		// Parse the message.
		String msg = String::utf8((const char *)buffer, buffer_pos);
		String output = _process_message(msg);

		// Format response with content-length header.
		CharString c_res = output.utf8();
		CharString c_header = vformat("Content-Length: %d\r\n\r\n", c_res.length()).utf8();

		// Queue full response.
		response_queue.push_back(c_header);
		response_queue.push_back(c_res);

		// Reset for next message.
		buffer_pos = 0;
		has_header = false;
		has_content = false;
		content_length = 0;
	}

	return OK;
}

Error GodotCLIServer::CLIRequest::send_data() {
	if (connection.is_null()) {
		return ERR_CONNECTION_ERROR;
	}

	while (responses_sent < response_queue.size()) {
		const CharString &data = response_queue[responses_sent];
		int written = 0;
		Error err = connection->put_partial_data((const uint8_t *)data.get_data(), data.length(), written);
		if (err != OK) {
			return err;
		}
		if (written < data.length()) {
			break; // Buffer full, try again next poll.
		}
		responses_sent++;
	}

	// Clean up sent responses.
	if (responses_sent >= response_queue.size()) {
		response_queue.clear();
		responses_sent = 0;
	}

	return OK;
}

#undef CLASS_NAME
#define CLASS_NAME GodotCLIServer

void GodotCLIServer::_bind_methods() {
}
