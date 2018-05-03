#pragma once
#include "zion.h"
#include <vector>
#include "location.h"
#include "logger_decls.h"
 
struct user_error : std::exception {
	user_error(log_level_t log_level, location_t location);
	user_error(log_level_t log_level, location_t location, const char *format...);
	user_error(log_level_t log_level, location_t location, const char *format, va_list args);
	user_error(location_t location, const char *format...);
	user_error(location_t location, const char *format, va_list args);

	virtual ~user_error() {}
	virtual const char *what() const noexcept;
	void add_info(location_t location, const char *format...);

	log_level_t log_level;
	location_t location;
	std::string message;

	ptr<std::vector<std::pair<location_t, std::string>>> extra_info;

	// Use print_exception externally, not display...
	void display() const;

	friend void print_exception(const user_error &e, int level);
};

struct unbound_type_error : std::exception {
	virtual ~unbound_type_error() {}
	virtual const char *what() const noexcept;

	unbound_type_error(location_t location, const char *format...);
	unbound_type_error(location_t location, const char *format, va_list args);
	user_error user_error;
};

void print_exception(const user_error &e, int level = 0);
