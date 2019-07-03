#pragma once
#include <memory>
#include <string>

#include "identifier.h"
#include "translate.h"
#include "types.h"

namespace bitter {
struct Expr;
}

struct Env : public TranslationEnv {
  using ref = const Env &;

  Env(const types::Scheme::Map &map,
      const std::shared_ptr<const types::Type> &return_type,
      std::shared_ptr<TrackedTypes> tracked_types,
      const CtorIdMap &ctor_id_map,
      const DataCtorsMap &data_ctors_map);

  types::Scheme::Map map;
  std::shared_ptr<const types::Type> return_type;

  types::Ref track(const bitter::Expr *expr, types::Ref type);
  types::Ref get_tracked_type(bitter::Expr *expr) const;
  types::Ref maybe_get_tracked_type(bitter::Expr *expr) const;
  void extend(Identifier id,
              const types::SchemeRef &scheme,
              bool allow_subscoping);
  void rebind_env(const types::Map &env);
  types::Scheme::Ref lookup_env(Identifier id) const;
  types::Scheme::Ref maybe_lookup_env(Identifier id) const;
  std::vector<std::pair<std::string, types::Refs>> get_ctors(
      types::Ref type) const;
  std::string str() const;
};

std::string str(const types::Scheme::Map &m);
