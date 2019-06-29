#pragma once

#include <string>

#include "location.h"

struct Context {
  Location location;
  std::string message;
};

Context make_context(Location location, const char *format, ...);
