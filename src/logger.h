#pragma once
#include <string>
#include <stdio.h>
#include <iosfwd>
#include <ostream>
#include <mutex>
#include "zion.h"
#include "logger_decls.h"
#include <list>
#include "utils.h"

const char *logstr(log_level_t ll);
void time_now(std::ostream &os, bool exact, bool for_humans);
void append_time(std::ostream &os, double time_exact, bool exact, bool for_humans, const char sep = ' ');

struct logger {
	virtual ~logger() throw() {}
	virtual void logv(log_level_t level, const location_t *location, const char *format, va_list args) = 0;
	virtual void log(log_level_t level, const location_t *location, const char *format, ...) = 0;
	virtual void dump() = 0;
};

class standard_logger : public logger {
public:
	standard_logger(const std::string &name, const std::string &root_file_path);
	virtual ~standard_logger();

	void logv(log_level_t level, const location_t *location, const char *format, va_list args);
	void log(log_level_t level, const location_t *location, const char *format, ...);
	void close();
	void open();
	void flush();
	void dump();

	friend void panic_(const char *filename, int line, std::string msg);
	friend void logv(log_level_t level, const char *format, va_list args);

private:
	std::mutex m_mutex;
	std::string m_name;
	std::string m_root_file_path;
	std::string m_current_logfile;
	FILE *m_fp;
};

struct tee_logger : public logger {
	tee_logger();
	virtual ~tee_logger() throw();

	virtual void logv(log_level_t level, const location_t *location, const char *format, va_list args);
	virtual void log(log_level_t level, const location_t *location, const char *format, ...);
	void dump();

	std::string captured_logs_as_string() const;

	logger *logger_old;
	std::list<std::tuple<log_level_t, maybe<location_t>, std::string>> captured_logs;
};

struct indent_logger : logger {
	indent_logger(location_t location, int level, std::string message);
	virtual ~indent_logger() throw();

	virtual void logv(log_level_t level, const location_t *location, const char *format, va_list args);
	virtual void log(log_level_t level, const location_t *location, const char *format, ...);
	virtual void dump();

	location_t location;
	std::string msg;
	int level;
	logger *logger_old;
};

#ifdef ZION_DEBUG
#define INDENT(level, message) \
	indent_logger _indent(INTERNAL_LOC(), level, message)
#else
#define INDENT(level, message)
#endif

struct note_logger : logger {
	note_logger(std::string message);
	virtual ~note_logger() throw();

	virtual void logv(log_level_t level, const location_t *location, const char *format, va_list args);
	virtual void log(log_level_t level, const location_t *location, const char *format, ...);
	virtual void dump();

	std::string msg;
	logger *logger_old;
};

void write_log_streamv(std::ostream &os, log_level_t level, const location_t *location, const char *format, va_list args);
