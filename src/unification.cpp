#include "unification.h"

#include <iostream>
#include <sstream>

#include "ast.h"
#include "dbg.h"
#include "logger.h"
#include "ptr.h"
#include "types.h"
#include "user_error.h"
#include "utils.h"
#include "zion.h"

namespace types {

types::SchemeRef scheme_unify(types::Scheme::Ref a, types::Scheme::Ref b) {
  if (a == nullptr || b == nullptr) {
    assert(false);
    return {};
  }
  // log("checking %s == %s", a->str().c_str(), b->str().c_str());
  // log("normalized checking %s == %s", a->normalize()->str().c_str(),
  // b->normalize()->str().c_str());
  if (a->normalize()->str() == b->normalize()->str()) {
    debug_above(4, log("found exact match between %s and %s", a->str().c_str(),
                       b->str().c_str()));
    return a;
  }

  auto ta = a->freshen()->type;
  auto tb = b->freshen()->type;
  debug_above(4,
              log("unifying %s and %s", ta->str().c_str(), tb->str().c_str()));
  auto unification = unify(ta, tb);
  if (!unification.result) {
    debug_above(4, log_location(unification.error_location,
                                "schemes %s and %s do not match because %s",
                                ta->str().c_str(), tb->str().c_str(),
                                unification.error_string.c_str()));
    return {};
  }

  auto scheme = ta->rebind(unification.bindings)->generalize({});
  assert(scheme->normalize()->repr() ==
         tb->rebind(unification.bindings)->generalize({})->normalize()->repr());
  return scheme;
}

bool scheme_equality(types::Scheme::Ref a, types::Scheme::Ref b) {
  if (a == nullptr || b == nullptr) {
    assert(false);
    return false;
  }
  // log("checking %s == %s", a->str().c_str(), b->str().c_str());
  // log("normalized checking %s == %s", a->normalize()->str().c_str(),
  // b->normalize()->str().c_str());
  if (a->normalize()->str() == b->normalize()->str()) {
    return true;
  }

  auto ta = a->freshen()->type;
  auto tb = b->freshen()->type;
  auto unification = unify(ta, tb);
  if (!unification.result) {
    debug_above(4, log_location(unification.error_location,
                                "schemes %s and %s do not match because %s",
                                ta->str().c_str(), tb->str().c_str(),
                                unification.error_string.c_str()));
  }
  return unification.result;
}

bool type_equality(types::Ref a, types::Ref b) {
  if (a == b) {
    /* the same pointer */
    return true;
  }

  if (auto ti_a = dyncast<const TypeId>(a)) {
    if (auto ti_b = dyncast<const TypeId>(b)) {
      return ti_a->id.name == ti_b->id.name;
    } else {
      return false;
    }
  } else if (auto tv_a = dyncast<const TypeVariable>(a)) {
    if (auto tv_b = dyncast<const TypeVariable>(b)) {
      return tv_a->id.name == tv_b->id.name;
    } else {
      return false;
    }
  } else if (auto to_a = dyncast<const TypeOperator>(a)) {
    if (auto to_b = dyncast<const TypeOperator>(b)) {
      return (type_equality(to_a->oper, to_b->oper) &&
              type_equality(to_a->operand, to_b->operand));
    } else {
      return false;
    }
  } else if (auto tup_a = dyncast<const TypeTuple>(a)) {
    if (auto tup_b = dyncast<const TypeTuple>(b)) {
      if (tup_a->dimensions.size() != tup_b->dimensions.size()) {
        return false;
      }
      for (size_t i = 0; i < tup_a->dimensions.size(); ++i) {
        if (!type_equality(tup_a->dimensions[i], tup_b->dimensions[i])) {
          return false;
        }
      }
      return true;
    }
  } else {
    auto error = zion::user_error(
        a->get_location(),
        "type_equality is not implemented between these two types");
    error.add_info(b->get_location(), "%s and %s", a->str().c_str(),
                   b->str().c_str());
    throw error;
  }
  return false;
}

inline bool occurs_check(std::string a, Ref type) {
  return in(a, type->get_ftvs());
}

Unification bind(std::string a, Ref type) {
  /* return a Unification which is the result of substitution [type/a] */

  /* first do an occurs check */
  if (type->get_ftvs().count(a) != 0) {
    /* this type exists within its own substitution. Fail. */
    return Unification{false,
                       type->get_location(),
                       string_format("infinite type detected! %s = %s",
                                     a.c_str(), type->str().c_str()),
                       {}};
  }

  return Unification{true, type->get_location(), "", Map{{a, type}}};
}

Unification unify(Ref a, Ref b) {
  assert(a != nullptr);
  assert(b != nullptr);
  debug_above(8, log("unify(%s, %s)", a->str().c_str(), b->str().c_str()));
  if (type_equality(a, b)) {
    return Unification{true, INTERNAL_LOC(), "", {}};
  }

  if (auto tv_a = dyncast<const TypeVariable>(a)) {
    return bind(tv_a->id.name, b);
  } else if (auto tv_b = dyncast<const TypeVariable>(b)) {
    return bind(tv_b->id.name, a);
  } else if (auto to_a = dyncast<const TypeOperator>(a)) {
    if (auto to_b = dyncast<const TypeOperator>(b)) {
      return unify_many({to_a->oper, to_a->operand},
                        {to_b->oper, to_b->operand});
    }
  } else if (auto tup_a = dyncast<const TypeTuple>(a)) {
    if (auto tup_b = dyncast<const TypeTuple>(b)) {
      return unify_many(tup_a->dimensions, tup_b->dimensions);
    }
  }

  auto location = best_location(a->get_location(), b->get_location());
  return Unification{
      false,
      location,
      string_format("type error. %s != %s", a->str().c_str(), b->str().c_str()),
      {},
  };
}

types::Map compose(const types::Map &a, const types::Map &b) {
  debug_above(11,
              log("composing {%s} with {%s}",
                  join_with(a, ", ",
                            [](const auto &pair) {
                              return string_format("%s: %s", pair.first.c_str(),
                                                   pair.second->str().c_str());
                            })
                      .c_str(),
                  join_with(b, ", ", [](const auto &pair) {
                    return string_format("%s: %s", pair.first.c_str(),
                                         pair.second->str().c_str());
                  }).c_str()));
  types::Map m;
  for (auto pair : b) {
    m[pair.first] = pair.second->rebind(a);
  }
  for (auto pair : a) {
    debug_above(11, log("-- check %s in %s when going to assign it to %s -- ",
                        pair.first.c_str(), str(m).c_str(),
                        pair.second->str().c_str()));
    assert(!in(pair.first, m));
    m[pair.first] = pair.second;
  }
  debug_above(11, log("which gives: %s",
                      join_with(m, ", ", [](const auto &pair) {
                        return string_format("%s: %s", pair.first.c_str(),
                                             pair.second->str().c_str());
                      }).c_str()));
  return m;
}

Unification compose(const Unification &a, const Unification &b) {
  if (a.result && b.result) {
    return Unification{true, INTERNAL_LOC(), "",
                       compose(a.bindings, b.bindings)};
  } else {
    return Unification{false,
                       a.result ? b.error_location : a.error_location,
                       a.result ? b.error_string : a.error_string,
                       {}};
  }
}

std::vector<Ref> rebind_tails(const std::vector<Ref> &types,
                              const Map &bindings) {
  assert(1 <= types.size());
  std::vector<Ref> new_types;
  for (size_t i = 1; i < types.size(); ++i) {
    new_types.push_back(types[i]->rebind(bindings));
  }
  return new_types;
}

Unification unify_many(const types::Refs &as, const types::Refs &bs) {
  debug_above(8, log("unify_many([%s], [%s])", join_str(as, ", ").c_str(),
                     join_str(bs, ", ").c_str()));
  Location location = as.size() != 0
                          ? (bs.size() != 0
                                 ? best_location(as[0]->get_location(),
                                                 bs[0]->get_location())
                                 : as[0]->get_location())
                          : (bs.size() != 0 ? bs[0]->get_location()
                                            : INTERNAL_LOC());
  if (as.size() == 0 && bs.size() == 0) {
    return Unification{true, location, "", {}};
  } else if (as.size() != bs.size()) {
    return Unification{false,
                       location,
                       string_format("unification mismatch %s != %s",
                                     str(as).c_str(), str(bs).c_str()),
                       {}};
    /*
    throw zion::user_error(
        as.size() != 0
            ? as[0]->get_location()
            : (bs.size() != 0 ? bs[0]->get_location() : INTERNAL_LOC()),
        "unification mismatch %s != %s", join_str(as, " -> ").c_str(),
        join_str(bs, " -> ").c_str());
        */
  }

  auto u1 = unify(as[0], bs[0]);
  auto u2 = unify_many(rebind_tails(as, u1.bindings),
                       rebind_tails(bs, u1.bindings));
  return compose(u2, u1);
}

} // namespace types
