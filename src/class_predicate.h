#pragma once

#include <string>
#include <vector>

#include "types.h"

namespace types {

struct ClassPredicate final {
  typedef std::shared_ptr<const ClassPredicate> Ref;

  ClassPredicate() = delete;
  ClassPredicate(const ClassPredicate &) = delete;
  ClassPredicate(std::string classname, const Refs &params);
  ClassPredicate(std::string classname, const Identifiers &params);

  std::string repr() const;
  std::string str() const;

  std::string const classname;
  Refs const params;

  bool operator<(const ClassPredicate &rhs) const;

private:
  mutable bool has_repr_;
  mutable std::string repr_;
};

} // namespace types
