#include "constraint.h"

#include <cstdlib>

const bool dbg_show_constraints = std::getenv("CIDER_SHOW_CONSTRAINTS") !=
                                  nullptr;

namespace types {

Constraint::Constraint(types::Ref a, types::Ref b, Context &&context)
    : a(a), b(b), context(std::move(context)) {
}

void Constraint::rebind(const types::Map &env) {
  a = a->rebind(env);
  b = b->rebind(env);
}

std::string Constraint::str() const {
  return string_format("%s == %s because %s", a->str().c_str(),
                       b->str().c_str(), context.message.c_str());
}

void append_to_constraints(Constraints &constraints,
                           types::Ref a,
                           types::Ref b,
                           Context &&context) {
  if (dbg_show_constraints) {
    log_location(context.location, "constraining a: %s b: %s because %s",
                 a->str().c_str(), b->str().c_str(), context.message.c_str());
    log_location(a->get_location(), "a: %s", a->str().c_str());
    log_location(b->get_location(), "b: %s", b->str().c_str());
  }
  assert(a != nullptr);
  assert(b != nullptr);
  constraints.push_back({a, b, std::move(context)});
}

void rebind_constraints(Constraints::iterator iter,
                        const Constraints::iterator &end,
                        const Map &bindings) {
  if (bindings.size() == 0) {
    /* nothing to rebind, bail */
    return;
  }

  while (iter != end) {
    (*iter++).rebind(bindings);
  }
}

} // namespace types

std::string str(const types::Constraints &constraints) {
  std::stringstream ss;
  ss << "[";
  const char *delim = "";
  for (auto c : constraints) {
    ss << delim << c.str();
    delim = ", ";
  }
  ss << "]";
  return ss.str();
}
