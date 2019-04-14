#pragma once

#include "resolver.h"

#include <string>

#include "llvm_utils.h"

namespace gen {

struct strict_resolver_t final : public resolver_t {
  strict_resolver_t(llvm::Value *llvm_value);
  ~strict_resolver_t();
  llvm::Value *resolve_impl() override;
  std::string str() const override;
  location_t get_location() const override;

private:
  llvm::Value *llvm_value;
};

struct lazy_resolver_t final : public resolver_t {
  /* there is a resolver for each top-level symbol, since top-level symbols can
   * reference one another */
  lazy_resolver_t(std::string name,
                  types::type_t::ref type,
                  lazy_resolver_callback_t &&callback);
  ~lazy_resolver_t();
  llvm::Value *resolve_impl() override;
  std::string str() const override;
  location_t get_location() const override;

private:
  /* by using a topographical sorting algorithm, we can enable re-entrancy in
   * order to resolve prototypes (basically forward decls) */
  enum sort_color_t {
    sc_unresolved,
    sc_resolving,
    sc_resolved,
  };
  sort_color_t sort_color;
  std::string name;
  types::type_t::ref type;
  lazy_resolver_callback_t callback;
  llvm::Value *value = nullptr;
};

} // namespace gen
