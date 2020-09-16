#pragma once
#include <set>

#include "data_ctors_map.h"
#include "types.h"

namespace match {
struct Pattern;
struct Nothing;

struct Pattern {
  typedef std::shared_ptr<const Pattern> Ref;

  Location location;

  Pattern(Location location) : location(location) {
  }
  virtual ~Pattern() {
  }

  virtual std::shared_ptr<const Nothing> asNothing() const {
    return nullptr;
  }
  virtual std::string str() const = 0;
};

extern std::shared_ptr<Nothing> theNothing;
Pattern::Ref intersect(Pattern::Ref lhs, Pattern::Ref rhs);
Pattern::Ref difference(Pattern::Ref lhs, Pattern::Ref rhs);
Pattern::Ref pattern_union(Pattern::Ref lhs, Pattern::Ref rhs);
Pattern::Ref all_of(Location location,
                    maybe<Identifier> expr,
                    const zion::DataCtorsMap &data_ctors_map,
                    types::Ref type);
} // namespace match
