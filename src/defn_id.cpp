#include "defn_id.h"

#include "ptr.h"
#include "types.h"
#include "user_error.h"

namespace types {

Location DefnId::get_location() const {
  return id.location;
}

std::string DefnId::str() const {
  return C_VAR + repr() + C_RESET;
}

std::string DefnId::repr() const {
  if (cached_repr.size() != 0) {
    return cached_repr;
  } else {
    cached_repr = "\"" + id.name + " :: " + scheme->repr() + "\"";
    return cached_repr;
  }
}

Identifier DefnId::repr_id() const {
  return {repr(), id.location};
}

bool DefnId::operator<(const DefnId &rhs) const {
  return repr() < rhs.repr();
}

std::ostream &operator<<(std::ostream &os, const DefnId &defn_id) {
  return os << defn_id.str();
}

void insert_needed_defn(NeededDefns &needed_defns,
                        const DefnId &defn_id,
                        Location location,
                        const DefnId &for_defn_id) {
  debug_above(1, log_location(location, "adding a needed defn for %s",
                              for_defn_id.str().c_str()));
  needed_defns[defn_id].push_back({location, for_defn_id});
}

} // namespace types
