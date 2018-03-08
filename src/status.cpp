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
