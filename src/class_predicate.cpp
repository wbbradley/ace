#include "class_predicate.h"

#include <ctype.h>
#include <sstream>

#include "colors.h"
#include "types.h"

namespace types {

ClassPredicate::ClassPredicate(std::string classname, const types::Refs &params)
    : classname(classname), params(params) {
#ifdef ZION_DEBUG
  assert(isupper(classname[0]));
#endif
}

bool ClassPredicate::operator<(const ClassPredicate &rhs) const {
  if (classname < rhs.classname) {
    return true;
  }

  for (int i = 0; i < params.size(); ++i) {
    if (i >= rhs.params.size()) {
      return false;
    } else if (params[i] < rhs.params[i]) {
      return true;
    }
  }
  return false;
}

std::string ClassPredicate::repr() const {
  if (!has_repr_) {
    std::stringstream ss;
    ss << classname;
    for (auto &param : params) {
      ss << " " << param->repr();
    }
    repr_ = ss.str();
  }

  return repr_;
}

std::string ClassPredicate::str() const {
  std::stringstream ss;
  ss << C_GOOD << repr() << C_RESET;
  return ss.str();
}

} // namespace types
