#include <stdarg.h>
#include <exception>
#include "user_error.h"
#include "logger.h"
#include "ast.h"

void print_exception(const user_error &e, int level) {
	try {
		std::rethrow_if_nested(e);
	} catch(const user_error &e) {
		print_exception(e, level+1);
	} catch(...) {
	}
	e.display();
}

user_error::user_error(log_level_t log_level, location_t location) :
	log_level(log_level),
	location(location),
   	extra_info(make_ptr<std::vector<std::pair<location_t, std::string>>>())
{
}

user_error::user_error(log_level_t log_level, location_t location, const char *format...) :
	user_error(log_level, location)
{
	va_list args;
	va_start(args, format);
	message = string_formatv(format, args);
	va_end(args);

	if (getenv("STATUS_BREAK") != nullptr) {
		dbg();
	}
}

user_error::user_error(log_level_t log_level, location_t location, const char *format, va_list args) :
	user_error(log_level, location)
{
	message = string_formatv(format, args);

	if (getenv("STATUS_BREAK") != nullptr) {
		dbg();
	}
}

user_error::user_error(location_t location, const char *format...) :
	user_error(log_error, location)
{
	va_list args;
	va_start(args, format);
	message = string_formatv(format, args);
	va_end(args);

	if (getenv("STATUS_BREAK") != nullptr) {
		dbg();
	}
}

user_error::user_error(location_t location, const char *format, va_list args) :
	user_error(log_error, location)
{
	message = string_formatv(format, args);

	if (getenv("STATUS_BREAK") != nullptr) {
		dbg();
	}
}

const char *user_error::what() const noexcept {
	return message.c_str();
}

void user_error::display() const {
	log_location(log_level, location, "%s", what());
	if (extra_info != nullptr) {
		for (auto info : *extra_info) {
			log_location(log_info, info.first, "%s", info.second.c_str());
		}
	}
}

void user_error::add_info(location_t location, const char *format...) {
	va_list args;
	va_start(args, format);
	std::string info = string_formatv(format, args);
	va_end(args);
	extra_info->push_back({location, info});
}

unbound_type_error::unbound_type_error(location_t location, const char *format...) :
	user_error(log_error, location)
{
	va_list args;
	va_start(args, format);
	user_error.message = string_formatv(format, args);
	va_end(args);
}

unbound_type_error::unbound_type_error(location_t location, const char *format, va_list args) :
	user_error(log_error, location)
{
	user_error.message = string_formatv(format, args);
}

const char *unbound_type_error::what() const noexcept {
	return user_error.what();
}
