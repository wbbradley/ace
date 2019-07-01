#pragma once

#include <string>
#include <vector>

#include "identifier.h"
#include "types.h"

namespace types {

struct ClassPredicate final {
  typedef std::shared_ptr<const ClassPredicate> Ref;

  ClassPredicate() = delete;
  ClassPredicate(const ClassPredicate &) = delete;
  ClassPredicate(Identifier classname, const Refs &params);
  ClassPredicate(Identifier classname, const Identifiers &params);

  std::string repr() const;
  std::string str() const;

  Location get_location() const;
  Ref remap_vars(const std::map<std::string, std::string> &remapping) const;
  const Ftvs &get_ftvs() const;

  Identifier const classname;
  Refs const params;

  bool operator<(const ClassPredicate &rhs) const;

private:
  mutable bool has_repr_;
  mutable std::string repr_;
  mutable bool has_ftvs_;
  mutable Ftvs ftvs_;
};

} // namespace types
