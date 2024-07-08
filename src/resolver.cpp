#include "resolver.h"

#include <memory>
#include <string>

#include "dbg.h"
#include "resolver_impl.h"
#include "types.h"
#include "user_error.h"

namespace cider {

namespace gen {

Resolver::~Resolver() {
}

llvm::Value *Resolver::resolve(llvm::IRBuilder<> &builder, Location location) {
  try {
    return this->resolve_impl(builder, location);
  } catch (user_error &e) {
    e.add_info(location, "with %s", this->str().c_str());
    throw;
  }
}

std::shared_ptr<Resolver> strict_resolver(llvm::Value *llvm_value) {
  return std::make_shared<StrictResolver>(llvm_value);
}

std::shared_ptr<Resolver> lazy_resolver(std::string name,
                                        types::Ref type,
                                        LazyResolverCallback &&callback) {
  return std::make_shared<LazyResolver>(name, type, std::move(callback));
}

Publishable::Publishable(std::string name, llvm::Value **llvm_value)
    : name(name), llvm_value(llvm_value) {
}

Publishable::~Publishable() {
}

void Publishable::publish(llvm::Value *llvm_value_) const {
  debug_above(2, {
    if (llvm_value_)
      log("publishing " c_id("%s") " as %s", name.c_str(),
          llvm_print(llvm_value_).c_str());
  });
  *llvm_value = llvm_value_;
}

} // namespace gen

} // namespace cider
