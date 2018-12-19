#pragma once
#include <string>
#include <unordered_map>
#include "location.h"
#include "identifier.h"

struct scope_t {
	identifier_t const id;
	bool         const is_let;
};
