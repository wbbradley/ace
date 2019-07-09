#pragma once

#include <string>

#include "location.h"

namespace zion {

void init_host();
int get_host_int(Location location, std::string name);

} // namespace zion
