#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "identifier.h"
#include "scheme.h"
#include "types.h"

namespace types {
struct Scheme;
struct Type;

struct DefnId {
  DefnId(Identifier const id, const types::SchemeRef &scheme)
      : id(id), scheme(scheme) {
  }

  Identifier const id;
  types::SchemeRef const scheme;

private:
  mutable std::string cached_repr;
  std::string repr() const;
  Identifier repr_id() const;

public:
  std::string repr_public() const {
    return repr();
  }
  Location get_location() const;
  std::string str() const;
  bool operator<(const DefnId &rhs) const;
};

struct DefnRef {
  Location location;
  DefnId from_defn_id;
};

typedef std::map<DefnId, std::vector<DefnRef>> NeededDefns;
std::ostream &operator<<(std::ostream &os, const DefnId &defn_id);

void insert_needed_defn(NeededDefns &needed_defns,
                        const DefnId &defn_id,
                        Location location,
                        const DefnId &from_defn_id);

} // namespace types
