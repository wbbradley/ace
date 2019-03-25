#pragma once
#include <string>
#include <unordered_map>

#include "identifier.h"
#include "location.h"

struct scope_t {
    identifier_t const id;
    bool const is_let;
};
