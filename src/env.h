#pragma once
#include <set>
#include <string>
#include <unordered_map>

#include "identifier.h"
#include "translate.h"
#include "types.h"

namespace types {
struct type_t;
struct scheme_t;
}; // namespace types

struct instance_requirement_t {
  std::string type_class_name;
  location_t location;
  types::type_t::ref type;
  std::string str() const;
};

namespace bitter {
struct expr_t;
}

struct env_t : public translation_env_t {
  using ref = const env_t &;

  env_t(const types::scheme_t::map &map,
        const std::shared_ptr<const types::type_t> &return_type,
        const std::vector<instance_requirement_t> &instance_requirements,
        std::shared_ptr<tracked_types_t> tracked_types,
        const ctor_id_map_t &ctor_id_map,
        const data_ctors_map_t &data_ctors_map)
      : translation_env_t(tracked_types, ctor_id_map, data_ctors_map), map(map),
        return_type(return_type), instance_requirements(instance_requirements) {
  }

  types::scheme_t::map map;
  std::shared_ptr<const types::type_t> return_type;
  std::vector<instance_requirement_t> instance_requirements;

  types::type_t::ref track(const bitter::expr_t *expr, types::type_t::ref type);
  types::type_t::ref get_tracked_type(bitter::expr_t *expr) const;
  types::type_t::ref maybe_get_tracked_type(bitter::expr_t *expr) const;
  void add_instance_requirement(const instance_requirement_t &ir);
  void extend(identifier_t id,
              std::shared_ptr<types::scheme_t> scheme,
              bool allow_subscoping);
  void rebind(const types::type_t::map &env);
  types::predicate_map_t get_predicate_map() const;
  std::shared_ptr<const types::type_t> lookup_env(identifier_t id) const;
  std::shared_ptr<const types::type_t> maybe_lookup_env(identifier_t id) const;
  std::vector<std::pair<std::string, types::type_t::refs>> get_ctors(
      types::type_t::ref type) const;
  std::string str() const;
};

std::string str(const types::scheme_t::map &m);
