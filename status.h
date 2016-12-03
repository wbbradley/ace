#pragma once
#include "location.h"
#include "logger_decls.h"

struct status_t {
	template <typename T>
	status_t(T t) = delete;
	status_t(bool fail=false) : fail(fail) {}

	operator bool() const { return !fail; }
	status_t operator |=(const status_t rhs);

	void emit_message(log_level_t log_level, location location, const char *format, ...);
	void emit_messagev(log_level_t log_level, location location, const char *format, va_list args);

	bool reported_on_error_at(location) const;

	/* status can only get worse, so, make sure no one sets fail directly to
	 * false except the ctor */
private:
	bool fail = false;
	location last_error_location;
};

namespace ast { struct item; }
void user_message(log_level_t level, status_t &status, const ast::item &item, const char *msg, ...);
void user_message(log_level_t level, status_t &status, location location, const char *format, ...);

template <typename Loc>
void user_error(status_t &status, const Loc &loc, const char *msg) {
	user_message(log_error, status, loc, msg);
}

template <typename Loc, typename Arg1>
void user_error(status_t &status, const Loc &loc, const char *msg, Arg1 arg1) {
	user_message(log_error, status, loc, msg, arg1);
}

template <typename Loc, typename Arg1, typename Arg2>
void user_error(status_t &status, const Loc &loc, const char *msg, Arg1 arg1, Arg2 arg2) {
	user_message(log_error, status, loc, msg, arg1, arg2);
}

template <typename Loc, typename Arg1, typename Arg2, typename Arg3>
void user_error(status_t &status, const Loc &loc, const char *msg, Arg1 arg1, Arg2 arg2, Arg3 arg3) {
	user_message(log_error, status, loc, msg, arg1, arg2, arg3);
}

template <typename Loc, typename Arg1, typename Arg2, typename Arg3, typename Arg4>
void user_error(status_t &status, const Loc &loc, const char *msg, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4) {
	user_message(log_error, status, loc, msg, arg1, arg2, arg3, arg4);
}

template <typename Loc>
void user_info(status_t &status, const Loc &loc, const char *msg) {
	user_message(log_info, status, loc, msg);
}

template <typename Loc, typename Arg1>
void user_info(status_t &status, const Loc &loc, const char *msg, Arg1 arg1) {
	user_message(log_info, status, loc, msg, arg1);
}

template <typename Loc, typename Arg1, typename Arg2>
void user_info(status_t &status, const Loc &loc, const char *msg, Arg1 arg1, Arg2 arg2) {
	user_message(log_info, status, loc, msg, arg1, arg2);
}

template <typename Loc, typename Arg1, typename Arg2, typename Arg3>
void user_info(status_t &status, const Loc &loc, const char *msg, Arg1 arg1, Arg2 arg2, Arg3 arg3) {
	user_message(log_info, status, loc, msg, arg1, arg2, arg3);
}

template <typename Loc, typename Arg1, typename Arg2, typename Arg3, typename Arg4>
void user_info(status_t &status, const Loc &loc, const char *msg, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4) {
	user_message(log_info, status, loc, msg, arg1, arg2, arg3, arg4);
}
