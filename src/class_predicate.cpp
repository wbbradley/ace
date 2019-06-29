#include "class_predicate.h"

#include <ctype.h>
#include <sstream>

#include "colors.h"
#include "types.h"

namespace types {

ClassPredicate::ClassPredicate(std::string classname,
                               const types::Type::Refs &tvs)
    : classname(classname), tvs(tvs) {
#ifdef ZION_DEBUG
  assert(isupper(classname[0]));
#endif
}

bool ClassPredicate::operator<(const ClassPredicate &rhs) const {
  if (classname < rhs.classname) {
    return true;
  }

  for (int i = 0; i < tvs.size(); ++i) {
    if (i >= rhs.tvs.size()) {
      return false;
    } else if (tvs[i] < rhs.tvs[i]) {
      return true;
    }
  }
  return false;
}

std::string ClassPredicate::repr() const {
  if (!has_repr_) {
    std::stringstream ss;
    ss << classname;
    for (auto &tv : tvs) {
      ss << " " << tv->repr();
    }
    repr_ = ss.str();
  }

  return repr_;
}

std::string ClassPredicate::str() const {
  std::stringstream ss;
  ss << C_TYPE << classname << C_RESET;
  for (auto &tv : tvs) {
    ss << " " << C_ID << tv << C_RESET;
  }
  repr_ = ss.str();
}

} // namespace types
