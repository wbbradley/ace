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
  types::Type::ref type;
  std::string str() const;
};

namespace bitter {
struct Expr;
}

struct Env : public TranslationEnv {
  using ref = const Env &;

  Env(const types::Scheme::map &map,
      const std::shared_ptr<const types::Type> &return_type,
      const std::vector<InstanceRequirement> &instance_requirements,
      std::shared_ptr<tracked_types_t> tracked_types,
      const ctor_id_map_t &ctor_id_map,
      const data_ctors_map_t &data_ctors_map)
      : TranslationEnv(tracked_types, ctor_id_map, data_ctors_map), map(map),
        return_type(return_type), instance_requirements(instance_requirements) {
  }

  types::Scheme::map map;
  std::shared_ptr<const types::Type> return_type;
  std::vector<InstanceRequirement> instance_requirements;

  types::Type::ref track(const bitter::Expr *expr, types::Type::ref type);
  types::Type::ref get_tracked_type(bitter::Expr *expr) const;
  types::Type::ref maybe_get_tracked_type(bitter::Expr *expr) const;
  void add_instance_requirement(const InstanceRequirement &ir);
  void extend(Identifier id,
              std::shared_ptr<types::Scheme> scheme,
              bool allow_subscoping);
  void rebind_env(const types::Type::map &env);
  types::predicate_map_t get_predicate_map() const;
  std::shared_ptr<const types::Type> lookup_env(Identifier id) const;
  std::shared_ptr<const types::Type> maybe_lookup_env(Identifier id) const;
  std::vector<std::pair<std::string, types::Type::refs>> get_ctors(
      types::Type::ref type) const;
  std::string str() const;
};

std::string str(const types::Scheme::map &m);
