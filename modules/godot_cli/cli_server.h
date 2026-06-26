/**************************************************************************/
/*  cli_server.h                                                          */
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

#include "core/io/json.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/object/object.h"
#include "modules/godot_cli/cli_types.h"

class GodotCLICommandHandler;

/**
 * TCP server that receives JSON commands from the godot-cli client,
 * routes them to the command handler, and sends back responses.
 *
 * Singleton pattern (like GDScriptLanguageProtocol).
 */
class GodotCLIServer : public Object {
	GDCLASS(GodotCLIServer, Object);

	static GodotCLIServer *singleton;

	struct CLIRequest {
		GodotCLIServer *server = nullptr;
		Ref<StreamPeerTCP> connection;
		uint8_t buffer[godot_cli::MAX_MESSAGE_SIZE];
		int buffer_pos = 0;
		int content_length = 0;
		bool has_header = false;
		bool has_content = false;

		// Queue of responses to send.
		Vector<CharString> response_queue;
		int responses_sent = 0;

		Error handle_data();
		Error send_data();
	};

	Ref<TCPServer> server;
	int listen_port = godot_cli::DEFAULT_PORT;
	bool running = false;

	// For simplicity, we handle one client at a time.
	CLIRequest *current_client = nullptr;

	// Command dispatcher.
	GodotCLICommandHandler *handler = nullptr;

	Error _on_client_connected();
	void _on_client_disconnected();
	Dictionary _parse_command(const String &p_message);

	// Called from CLIRequest::handle_data.
	String process_message(const String &p_message);

protected:
	static void _bind_methods();

public:
	static GodotCLIServer *get_singleton() { return singleton; }

	Error start(int p_port);
	void stop();
	bool is_running() const { return running; }
	int get_port() const { return listen_port; }

	// Called from main loop iteration to process network events.
	void poll();

	GodotCLIServer();
	~GodotCLIServer() override;
};
