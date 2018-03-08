#include <stdarg.h>
#include <exception>
#include "status.h"
#include "logger.h"
#include "ast.h"

void print_exception(const user_error_t &e, int level) {
    try {
        std::rethrow_if_nested(e);
    } catch(const user_error_t &e) {
        print_exception(e, level+1);
    } catch(...) {
	}
	e.display();
}

status_t status_t::operator |=(const status_t rhs) {
	fail |= rhs.fail;
	return *this;
}

user_error_t::user_error_t(location_t location, const char *format...) :
   	location(location), extra_info(make_ptr<std::vector<std::pair<location_t, std::string>>>())
{
	va_list args;
	va_start(args, format);
	message = string_formatv(format, args);
	va_end(args);
}

user_error_t::user_error_t(location_t location, const char *format, va_list args) :
   	location(location), extra_info(make_ptr<std::vector<std::pair<location_t, std::string>>>())
{
	message = string_formatv(format, args);
}

const char *user_error_t::what() const noexcept {
	return message.c_str();
}

void user_error_t::display() const {
	log_location(log_error, location, "%s", what());
	if (extra_info != nullptr) {
		for (auto info : *extra_info) {
			log_location(log_info, info.first, "%s", info.second.c_str());
		}
	}
}

void user_error_t::add_info(location_t location, const char *format...) {
	va_list args;
	va_start(args, format);
	std::string info = string_formatv(format, args);
	va_end(args);
	extra_info->push_back({location, info});
}

bool status_t::reported_on_error_at(location_t l) const {
   	return fail && last_error_location.str() == l.str();
}

void status_t::emit_message(log_level_t level, location_t location, const char *format, ...) {	
	va_list args;
	va_start(args, format);
	emit_messagev(level, location, format, args);
	va_end(args);
}

void status_t::emit_messagev(log_level_t level, location_t location, const char *format, va_list args) {	
	if (level == log_error) {
		if (fail && getenv("STATUS_BREAK")) {
			write_fp(stderr, "Status already failed. Breaking...\n");
			dbg();
		}

		fail = true;
		last_error_location = location;
		throw user_error_t(location, format, args);
	}

	logv_location(level, location, format, args);

    if (fail && getenv("STATUS_BREAK")) {
        write_fp(stderr, "Status changed. Breaking...\n");
        dbg();
    }
}

void user_message(log_level_t level, status_t &status, const ast::item_t &item, const char *format, ...) {
    va_list args;
    va_start(args, format);
    auto str = string_formatv(format, args);
    va_end(args);

    status.emit_message(level, item.token.location, "%s", str.c_str());
}

void user_message(log_level_t level, status_t &status, location_t location, const char *format, ...) {
    va_list args;
    va_start(args, format);
    auto str = string_formatv(format, args);
    va_end(args);

    status.emit_message(level, location, "%s", str.c_str());
	// throw std::logic_error(str);
}

