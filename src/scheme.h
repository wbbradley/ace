#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace types {
struct Scheme;
typedef std::shared_ptr<const Scheme> SchemeRef;
typedef std::map<std::string, SchemeRef> SchemeMap;
} // namespace types

#include "class_predicate.h"
#include "location.h"
#include "types.h"

namespace types {

struct Scheme final : public std::enable_shared_from_this<Scheme> {
  typedef std::shared_ptr<const Scheme> Ref;
  typedef std::vector<Ref> Refs;
  typedef std::map<std::string, Ref> Map;

  Scheme(const std::vector<std::string> &vars,
         const ClassPredicates &predicates,
         types::Ref type);

  types::Ref instantiate(Location location) const;
  Scheme::Ref rebind(const types::Map &env) const;
  Scheme::Ref normalize() const;

  Scheme::Ref freshen() const;

  /* count of the constrained type variables */
  int btvs() const;
  Ftvs ftvs() const;

  std::string str() const;
  std::string repr() const;
  Location get_location() const;

  std::vector<std::string> const vars;
  ClassPredicates const predicates;
  types::Ref const type;
};

} // namespace types

types::SchemeRef scheme(std::vector<std::string> vars,
                        const types::ClassPredicates &predicates,
                        const types::Ref &type);
