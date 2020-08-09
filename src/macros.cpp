#include "macros.h"
#include "pantor/inja.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace zion {
namespace macro {
bool unit_test() {
  json data;
  data["name"] = "world";

  return inja::render("Hello {{ name }}!", data) == "Hello world!";
}
} // namespace macro
} // namespace zion
