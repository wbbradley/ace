#include "status.h"
#include "logger_decls.h"
#include "ast.h"

bool status_t::reported_on_error_at(location l) const {
   	return fail && last_error_location.str() == l.str();
}

void status_t::emit_message(log_level_t level, location location, const char *format, ...) {	
	va_list args;
	va_start(args, format);
	emit_messagev(level, location, format, args);
	va_end(args);
}

void status_t::emit_messagev(log_level_t level, location location, const char *format, va_list args) {	
	if (level == log_error) {
		fail = true;
		last_error_location = location;
	}
	logv_location(level, location, format, args);
}

void user_message(log_level_t level, status_t &status, const ast::item &item, const char *format, ...) {
    va_list args;
    va_start(args, format);
    auto str = string_formatv(format, args);
    va_end(args);

    status.emit_message(level, item.token.location, "%s", str.c_str());
}

void user_message(log_level_t level, status_t &status, location location, const char *format, ...) {
    va_list args;
    va_start(args, format);
    auto str = string_formatv(format, args);
    va_end(args);

    status.emit_message(level, location, "%s", str.c_str());
}

