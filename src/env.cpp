#include "env.h"

#include <iostream>

#include "ast.h"
#include "class_predicate.h"
#include "dbg.h"
#include "types.h"
#include "user_error.h"

Env::Env(const types::Scheme::Map &map,
         const std::shared_ptr<const types::Type> &return_type,
         std::shared_ptr<TrackedTypes> tracked_types,
         const CtorIdMap &ctor_id_map,
         const DataCtorsMap &data_ctors_map)
    : TranslationEnv(tracked_types, ctor_id_map, data_ctors_map), map(map),
      return_type(return_type) {
}

std::vector<std::pair<std::string, types::Refs>> Env::get_ctors(
    types::Ref type) const {
  return {};
}

types::Scheme::Ref Env::maybe_lookup_env(Identifier id) const {
  return get(map, id.name, types::Scheme::Ref{});
}

types::Scheme::Ref Env::lookup_env(Identifier id) const {
  auto type = maybe_lookup_env(id);
  if (type != nullptr) {
    return type;
  }
  auto error = user_error(id.location, "unbound variable " C_ID "%s" C_RESET,
                          id.name.c_str());
  /*
  for (auto pair : map) {
    error.add_info(pair.second->get_location(), "env includes %s :: %s",
                   pair.first.c_str(), pair.second->str().c_str());
  }
  */
  throw error;
}

void Env::rebind_env(const types::Map &bindings) {
  if (bindings.size() == 0) {
    return;
  }
  for (auto pair : map) {
    map[pair.first] = pair.second->rebind(bindings);
  }

  assert(tracked_types != nullptr);
  TrackedTypes temp_tracked_types;

  for (auto pair : *tracked_types) {
    assert(temp_tracked_types.count(pair.first) == 0);
    temp_tracked_types.insert({pair.first, pair.second->rebind(bindings)});
  }
  temp_tracked_types.swap(*tracked_types);
}

types::Ref Env::track(const bitter::Expr *expr, types::Ref type) {
  assert(tracked_types != nullptr);
  assert(!in(expr, *tracked_types));
  (*tracked_types)[expr] = type;
  return type;
}

types::Ref Env::get_tracked_type(bitter::Expr *expr) const {
  auto type = maybe_get_tracked_type(expr);
  if (type == nullptr) {
    throw user_error(expr->get_location(),
                     "could not find type for expression %s",
                     expr->str().c_str());
  }

  return type;
}

types::Ref Env::maybe_get_tracked_type(bitter::Expr *expr) const {
  assert(tracked_types != nullptr);
  auto iter = tracked_types->find(expr);
  return (iter != tracked_types->end()) ? iter->second : nullptr;
}

void Env::extend(Identifier id,
                 const types::Scheme::Ref &scheme,
                 bool allow_subscoping) {
  if (!allow_subscoping && in(id.name, map)) {
    throw user_error(
        id.location,
        "duplicate symbol " c_id("%s") " (TODO: make this error better)",
        id.name.c_str());
  }
  map[id.name] = scheme;
  debug_above(9, log("extending env with %s => %s", id.str().c_str(),
                     scheme->normalize()->str().c_str()));
}

std::string str(const types::Scheme::Map &m) {
  std::stringstream ss;
  ss << "{";
  ss << join_with(m, ", ", [](const auto &pair) {
    return string_format("%s: %s", pair.first.c_str(),
                         pair.second->str().c_str());
  });
  ss << "}";
  return ss.str();
}

std::string Env::str() const {
  std::stringstream ss;
  ss << "{context: " << ::str(map);
  if (return_type != nullptr) {
    ss << ", return_type: (" << return_type->str() << ")";
  }
  ss << "}";
  return ss.str();
}
