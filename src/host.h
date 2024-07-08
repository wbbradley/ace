#pragma once

#include <string>

#include "location.h"

namespace ace {

void init_host();
int get_host_int(Location location, std::string name);

} // namespace ace
