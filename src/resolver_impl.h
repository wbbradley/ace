#pragma once

#include <string>

#include "llvm_utils.h"
#include "resolver.h"

namespace zion {

namespace gen {

struct StrictResolver final : public Resolver {
  StrictResolver(llvm::Value *llvm_value);
  ~StrictResolver();
  llvm::Value *resolve_impl() override;
  std::string str() const override;
  Location get_location() const override;

private:
  llvm::Value *llvm_value;
};

struct LazyResolver final : public Resolver {
  /* there is a resolver for each top-level symbol, since top-level symbols can
   * reference one another */
  LazyResolver(std::string name,
               types::Ref type,
               LazyResolverCallback &&callback);
  ~LazyResolver();
  llvm::Value *resolve_impl() override;
  std::string str() const override;
  Location get_location() const override;

private:
  /* by using a topological sorting algorithm, we can enable re-entrancy in
   * order to resolve prototypes (basically forward decls) */
  enum SortColor {
    sc_unresolved,
    sc_resolving,
    sc_resolved,
  };
  SortColor sort_color;
  std::string name;
  types::Ref type;
  LazyResolverCallback callback;
  llvm::Value *value = nullptr;
};

} // namespace gen

} // namespace zion
