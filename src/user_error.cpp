#include "user_error.h"

#include <exception>
#include <stdarg.h>

#include "ast.h"
#include "dbg.h"
#include "logger.h"

namespace zion {

namespace {
bool errors_occurred_ = false;
}

bool user_error::errors_occurred() {
  return errors_occurred_;
}

void user_error::reset_errors_occurred() {
  errors_occurred_ = false;
}

void print_exception(const user_error &e, int level) {
  try {
    std::rethrow_if_nested(e);
  } catch (const user_error &e) {
    print_exception(e, level + 1);
  } catch (...) {
  }
  e.display();
}

user_error::user_error(log_level_t log_level, Location location)
    : log_level(log_level), location(location),
      extra_info(
          std::make_shared<std::vector<std::pair<Location, std::string>>>()) {
  errors_occurred_ = true;
}

user_error::user_error(log_level_t log_level,
                       Location location,
                       const char *format...)
    : user_error(log_level, location) {
  va_list args;
  va_start(args, format);
  message = string_formatv(format, args);
  va_end(args);

  if (getenv("STATUS_BREAK") != nullptr) {
    dbg();
  }
}

user_error::user_error(log_level_t log_level,
                       Location location,
                       const char *format,
                       va_list args)
    : user_error(log_level, location) {
  message = string_formatv(format, args);

  if (getenv("STATUS_BREAK") != nullptr) {
    dbg();
  }
}

user_error::user_error(Location location, const char *format...)
    : user_error(log_error, location) {
  va_list args;
  va_start(args, format);
  message = string_formatv(format, args);
  va_end(args);

  if (getenv("STATUS_BREAK") != nullptr) {
    dbg();
  }
}

user_error::user_error(Location location, const char *format, va_list args)
    : user_error(log_error, location) {
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
      log_location(info.first, "%s", info.second.c_str());
    }
  }
}

user_error &user_error::add_info(Location location, const char *format...) {
  va_list args;
  va_start(args, format);
  std::string info = string_formatv(format, args);
  va_end(args);
  extra_info->push_back({location, info});
  return *this;
}

} // namespace zion
