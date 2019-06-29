#pragma once
#include <string>
#include <unordered_map>

#include "identifier.h"
#include "location.h"

struct Scope {
  Identifier const id;
  bool const is_let;
};
