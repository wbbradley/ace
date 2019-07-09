#pragma once
#include <functional>
#include <memory>
#include <string>

#include "identifier.h"
#include "translate.h"
#include "types.h"

namespace bitter {
struct Expr;
}

namespace zion {

struct Env : public TranslationEnv {
  using ref = const Env &;

  Env(const std::shared_ptr<const types::Type> &return_type,
      std::shared_ptr<TrackedTypes> tracked_types,
      const CtorIdMap &ctor_id_map,
      const DataCtorsMap &data_ctors_map);

  std::shared_ptr<const types::Type> return_type;

  types::Ref track(const bitter::Expr *expr, types::Ref type);
  types::Ref get_tracked_type(bitter::Expr *expr) const;
  types::Ref maybe_get_tracked_type(bitter::Expr *expr) const;
  void rebind_env(const types::Map &env);
  std::vector<std::pair<std::string, types::Refs>> get_ctors(
      types::Ref type) const;
  std::string str() const;
};

} // namespace zion

std::string str(const types::Scheme::Map &m);
