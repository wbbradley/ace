#pragma once
#include <set>
#include <string>
#include <unordered_map>

#include "identifier.h"
#include "translate.h"
#include "types.h"

namespace types {
struct Type;
struct Scheme;
}; // namespace types

struct InstanceRequirement {
  std::string type_class_name;
  Location location;
  types::Ref type;
  std::string str() const;
};

namespace bitter {
struct Expr;
}

struct Env : public TranslationEnv {
  using ref = const Env &;

  Env(const types::Scheme::Map &map,
      const std::shared_ptr<const types::Type> &return_type,
      const std::vector<InstanceRequirement> &instance_requirements,
      std::shared_ptr<TrackedTypes> tracked_types,
      const CtorIdMap &ctor_id_map,
      const DataCtorsMap &data_ctors_map);

  types::Scheme::Map map;
  std::shared_ptr<const types::Type> return_type;
  std::vector<InstanceRequirement> instance_requirements;

  types::Ref track(const bitter::Expr *expr, types::Ref type);
  types::Ref get_tracked_type(bitter::Expr *expr) const;
  types::Ref maybe_get_tracked_type(bitter::Expr *expr) const;
  void add_instance_requirement(const InstanceRequirement &ir);
  void extend(Identifier id,
              std::shared_ptr<types::Scheme> scheme,
              bool allow_subscoping);
  void rebind_env(const types::Map &env);
  types::ClassPredicates get_predicate_map() const;
  types::Ref lookup_env(Identifier id) const;
  types::Ref maybe_lookup_env(Identifier id) const;
  std::vector<std::pair<std::string, types::Refs>> get_ctors(
      types::Ref type) const;
  std::string str() const;
};

std::string str(const types::Scheme::Map &m);
