#include "resolver.h"

#include <memory>
#include <string>

#include "resolver_impl.h"
#include "types.h"
#include "user_error.h"

namespace zion {

namespace gen {

Resolver::~Resolver() {
}

llvm::Value *Resolver::resolve() {
  try {
    return this->resolve_impl();
  } catch (user_error &e) {
    e.add_info(this->get_location(), "with %s", this->str().c_str());
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

Publishable::Publishable(llvm::Value **llvm_value) : llvm_value(llvm_value) {
}

Publishable::~Publishable() {
}

void Publishable::publish(llvm::Value *llvm_value_) const {
  *llvm_value = llvm_value_;
}

} // namespace gen

} // namespace zion
