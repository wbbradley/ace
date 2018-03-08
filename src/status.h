#pragma once
#include "zion.h"
#include <vector>
#include "location.h"
#include "logger_decls.h"
 
struct user_error_t {
	user_error_t(location_t location, const char *format...);
	user_error_t(location_t location, const char *format, va_list args);

	virtual ~user_error_t() {}
	virtual const char *what() const noexcept;
	void add_info(location_t location, const char *format...);

	location_t location;
	std::string message;
	ptr<std::vector<std::pair<location_t, std::string>>> extra_info;

	// Use print_exception externally, not display...
	void display() const;

	friend void print_exception(const user_error_t &e, int level);
};

void print_exception(const user_error_t &e, int level = 0);
