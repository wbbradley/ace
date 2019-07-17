#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "identifier.h"
#include "scheme.h"

namespace types {

struct DefnId {
  DefnId(Identifier const id, const types::Ref &type) : id(id), type(type) {
  }

  Identifier const id;
  types::Ref const type;

private:
  mutable std::string cached_repr;
  std::string repr() const;

public:
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
