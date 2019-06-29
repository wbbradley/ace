#include "typed_id.h"

std::string TypedId::repr() const {
  assert(id.name[0] != '(');
  if (cached_repr.size() != 0) {
    return cached_repr;
  } else {
    cached_repr = "\"" + id.name + " :: " + type->repr() + "\"";
    return cached_repr;
  }
}

std::ostream &operator<<(std::ostream &os, const TypedId &typed_id) {
  return os << typed_id.repr();
}

bool TypedId::operator<(const TypedId &rhs) const {
  return repr() < rhs.repr();
}
