#pragma once

#include <string>
#include "location.h"

struct context_t {
  location_t location;
  std::string message;
};

context_t make_context(location_t location, const char *format, ...);
