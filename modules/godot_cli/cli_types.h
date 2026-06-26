/**************************************************************************/
/*  cli_types.h                                                           */
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

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace godot_cli {

// Maximum JSON message size (4 MB).
constexpr int MAX_MESSAGE_SIZE = 4194304;

// Default port for the CLI daemon.
constexpr int DEFAULT_PORT = 3100;

// Protocol version for compatibility checking.
constexpr int PROTOCOL_VERSION = 1;

// Result codes.
enum ResultCode {
	RESULT_OK = 0,
	RESULT_ERROR = 1,
	RESULT_NOT_FOUND = 2,
	RESULT_INVALID_PARAMS = 3,
	RESULT_NOT_IMPLEMENTED = 4,
};

// Build a success response dictionary.
inline Dictionary make_response(int p_id, const Dictionary &p_result, const Dictionary &p_snapshot = Dictionary()) {
	Dictionary resp;
	resp["ok"] = true;
	resp["id"] = p_id;
	resp["result"] = p_result;
	if (!p_snapshot.is_empty()) {
		resp["snapshot"] = p_snapshot;
	}
	return resp;
}

// Build an error response dictionary.
inline Dictionary make_error(int p_id, const String &p_message, int p_code = RESULT_ERROR) {
	Dictionary resp;
	resp["ok"] = false;
	resp["id"] = p_id;
	resp["error"] = p_message;
	resp["code"] = p_code;
	return resp;
}

// Build a snapshot from the current scene tree (forward declaration).
Dictionary build_snapshot();

} // namespace godot_cli
