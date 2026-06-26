/**************************************************************************/
/*  log_collector.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT CLI MODULE                           */
/**************************************************************************/
/* Copyright (c) 2026 Andres Videla. MIT License.                         */
/**************************************************************************/

#pragma once

#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

/**
 * Captures print_line and print_error output into a ring buffer
 * so the AI agent can retrieve logs via debug/logs and debug/errors.
 */
class LogCollector : public Object {
	GDCLASS(LogCollector, Object);

	static LogCollector *singleton;
	static constexpr int MAX_LOG_LINES = 1000;
	static constexpr int MAX_ERROR_LINES = 200;

	struct LogEntry {
		double timestamp;
		String message;
	};

	Vector<LogEntry> log_entries;
	Vector<LogEntry> error_entries;

	static void _print_handler(const String &p_string);
	static void _error_handler(const String &p_string);

protected:
	static void _bind_methods();

public:
	static LogCollector *get_singleton() { return singleton; }

	void initialize();
	void finalize();

	Vector<LogEntry> get_recent_logs(int p_count = 50) const;
	Vector<LogEntry> get_recent_errors(int p_count = 50) const;

	void add_log(const String &p_msg);
	void add_error(const String &p_msg);

	LogCollector();
	~LogCollector() override;
};
