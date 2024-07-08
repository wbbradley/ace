#pragma once
#include <vector>

#include "location.h"
#include "logger_decls.h"
#include "ace.h"

namespace ace {

struct user_error : std::exception {
  user_error(LogLevel log_level, Location location);
  user_error(LogLevel log_level, Location location, const char *format...);
  user_error(LogLevel log_level,
             Location location,
             const char *format,
             va_list args);
  user_error(Location location, const char *format...);
  user_error(Location location, const char *format, va_list args);

  virtual ~user_error() {
  }
  virtual const char *what() const noexcept;
  user_error &add_info(Location location, const char *format...);

  LogLevel log_level;
  Location location;
  std::string message;

  std::shared_ptr<std::vector<std::pair<Location, std::string>>> extra_info;

  // Use print_exception externally, not display...
  void display() const;

  friend void print_exception(const user_error &e, int level);
  static bool errors_occurred();
  static void reset_errors_occurred();
};

void print_exception(const user_error &e, int level = 0);

} // namespace ace
