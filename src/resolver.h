#pragma once

#include <memory>
#include <string>

#include "llvm_utils.h"
#include "types.h"

namespace cider {

namespace gen {

enum ResolutionStatus {
  rs_resolve_again,
  rs_cache_resolution,
  rs_cache_global_load,
};

typedef std::function<ResolutionStatus(llvm::Value **)> LazyResolverCallback;

struct Publisher {
  virtual ~Publisher() {
  }
  virtual void publish(llvm::Value *llvm_value) const = 0;
};

struct Publishable : public Publisher {
  Publishable(std::string name, llvm::Value **llvm_value);
  ~Publishable();
  void publish(llvm::Value *llvm_value_) const override;

private:
  std::string name;
  llvm::Value **llvm_value;
};

struct Resolver {
  virtual ~Resolver() = 0;
  llvm::Value *resolve(llvm::IRBuilder<> &builder, Location location);
  virtual llvm::Value *resolve_impl(llvm::IRBuilder<> &builder,
                                    Location location) = 0;
  virtual std::string str() const = 0;
};

std::shared_ptr<Resolver> strict_resolver(llvm::Value *llvm_value);
std::shared_ptr<Resolver> lazy_resolver(std::string name,
                                        types::Ref type,
                                        LazyResolverCallback &&callback);

} // namespace gen

} // namespace cider
