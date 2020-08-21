#include "match.h"

#include <algorithm>
#include <numeric>
#include <typeinfo>

#include "ast.h"
#include "builtins.h"
#include "data_ctors_map.h"
#include "ptr.h"
#include "tld.h"
#include "translate.h"
#include "types.h"
#include "unification.h"
#include "user_error.h"
#include "zion.h"

namespace match {

struct Nothing : std::enable_shared_from_this<Nothing>, Pattern {
  Nothing() : Pattern(INTERNAL_LOC()) {
  }

  virtual std::shared_ptr<const Nothing> asNothing() const {
    return shared_from_this();
  }
  virtual std::string str() const;
};

struct CtorPatternValue {
  std::string type_name;
  std::string name;
  std::vector<Pattern::ref> args;

  std::string str() const;
};

struct CtorPattern : std::enable_shared_from_this<CtorPattern>, Pattern {
  CtorPatternValue cpv;
  CtorPattern(Location location, CtorPatternValue cpv)
      : Pattern(location), cpv(cpv) {
  }

  virtual std::string str() const;
};

struct CtorPatterns : std::enable_shared_from_this<CtorPatterns>, Pattern {
  std::vector<CtorPatternValue> cpvs;
  CtorPatterns(Location location, std::vector<CtorPatternValue> cpvs)
      : Pattern(location), cpvs(cpvs) {
  }

  virtual std::string str() const;
};

struct AllOf : std::enable_shared_from_this<AllOf>, Pattern {
  maybe<Identifier> name;
  const zion::DataCtorsMap &data_ctors_map;
  types::Ref type;

  AllOf(Location location,
        maybe<Identifier> name,
        const zion::DataCtorsMap &data_ctors_map,
        types::Ref type)
      : Pattern(location), name(name), data_ctors_map(data_ctors_map),
        type(type) {
  }

  virtual std::string str() const;
};

template <typename T>
struct Scalars : std::enable_shared_from_this<Scalars<T>>, Pattern {
  enum Kind { Include, Exclude } kind;
  std::set<T> collection;

  Scalars(Location location, Kind kind, std::set<T> collection)
      : Pattern(location), kind(kind), collection(collection) {
    assert_implies(kind == Include, collection.size() != 0);
  }

  static std::string scalar_name();

  virtual std::string str() const {
    switch (kind) {
    case Include: {
      std::string coll_str = "[" + ::join(collection, ", ") + "]";
      return coll_str;
    }
    case Exclude:
      if (collection.size() == 0) {
        return "all " + scalar_name();
      } else {
        std::string coll_str = "[" + ::join(collection, ", ") + "]";
        return "all " + scalar_name() + " except " + coll_str;
      }
    }
    assert(false);
    return "";
  }
};

template <> std::string Scalars<int64_t>::scalar_name() {
  static auto s = INT_TYPE + "s";
  return s;
}

template <> std::string Scalars<uint8_t>::scalar_name() {
  static auto s = CHAR_TYPE + "s";
  return s;
}

template <> std::string Scalars<double>::scalar_name() {
  static auto s = FLOAT_TYPE + "s";
  return s;
}

std::shared_ptr<const CtorPattern> asCtorPattern(Pattern::ref pattern) {
  return dyncast<const CtorPattern>(pattern);
}

std::shared_ptr<const CtorPatterns> asCtorPatterns(Pattern::ref pattern) {
  return dyncast<const CtorPatterns>(pattern);
}

std::shared_ptr<const AllOf> asAllOf(Pattern::ref pattern) {
  return dyncast<const AllOf>(pattern);
}

template <typename T>
std::shared_ptr<const Scalars<T>> asScalars(Pattern::ref pattern) {
  return dyncast<const Scalars<T>>(pattern);
}

std::shared_ptr<Nothing> theNothing = std::make_shared<Nothing>();
std::shared_ptr<Scalars<int64_t>> allIntegers =
    std::make_shared<Scalars<int64_t>>(INTERNAL_LOC(),
                                       Scalars<int64_t>::Exclude,
                                       std::set<int64_t>{});
std::shared_ptr<Scalars<uint8_t>> allChars = std::make_shared<Scalars<uint8_t>>(
    INTERNAL_LOC(),
    Scalars<uint8_t>::Exclude,
    std::set<uint8_t>{});
std::shared_ptr<Scalars<double>> allFloats = std::make_shared<Scalars<double>>(
    INTERNAL_LOC(),
    Scalars<double>::Exclude,
    std::set<double>{});

Pattern::ref all_of(Location location,
                    maybe<Identifier> expr,
                    const zion::DataCtorsMap &data_ctors_map,
                    types::Ref type) {
  return std::make_shared<match::AllOf>(location, expr, data_ctors_map, type);
}

Pattern::ref reduce_all_datatype(Location location,
                                 std::string type_name,
                                 Pattern::ref rhs,
                                 const std::vector<CtorPatternValue> &cpvs) {
  for (auto cpv : cpvs) {
    if (cpv.type_name != type_name) {
      auto error = zion::user_error(
          location,
          "invalid typed ctor pattern found. expected %s "
          "but ctor_pattern indicates it is a %s",
          type_name.c_str(), cpv.type_name.c_str());
      error.add_info(location, "comparing %s and %s", cpv.type_name.c_str(),
                     type_name.c_str());
      throw error;
    }
  }

  assert(cpvs.size() != 0);
  if (cpvs.size() == 1) {
    return std::make_shared<CtorPattern>(location, cpvs[0]);
  } else {
    return std::make_shared<CtorPatterns>(location, cpvs);
  }
}

Pattern::ref intersect(Location location,
                       const CtorPatternValue &lhs,
                       const CtorPatternValue &rhs) {
  assert(lhs.type_name == rhs.type_name);
  if (lhs.name != rhs.name) {
    return theNothing;
  }
  assert(lhs.args.size() == rhs.args.size());

  std::vector<Pattern::ref> reduced_args;
  reduced_args.reserve(lhs.args.size());

  for (size_t i = 0; i < lhs.args.size(); ++i) {
    auto new_arg = intersect(lhs.args[i], rhs.args[i]);
    if (dyncast<const Nothing>(new_arg) != nullptr) {
      return theNothing;
    } else {
      reduced_args.push_back(new_arg);
    }
  }
  assert(reduced_args.size() == lhs.args.size());
  return std::make_shared<CtorPattern>(
      location, CtorPatternValue{lhs.type_name, lhs.name, reduced_args});
}

Pattern::ref cpv_intersect(Pattern::ref lhs, const CtorPatternValue &rhs) {
  auto ctor_pattern = dyncast<const CtorPattern>(lhs);
  assert(ctor_pattern != nullptr);
  return intersect(lhs->location, ctor_pattern->cpv, rhs);
}

Pattern::ref intersect(Location location,
                       const std::vector<CtorPatternValue> &lhs,
                       const std::vector<CtorPatternValue> &rhs) {
  Pattern::ref intersection = theNothing;
  for (auto &cpv : lhs) {
    Pattern::ref init = std::make_shared<CtorPattern>(location, cpv);
    intersection = pattern_union(
        std::accumulate(rhs.begin(), rhs.end(), init, cpv_intersect),
        intersection);
  }
  return intersection;
}

template <typename T>
Pattern::ref intersect(const Scalars<T> &lhs, const Scalars<T> &rhs) {
  typename Scalars<T>::Kind new_kind;
  std::set<T> new_collection;

  if (lhs.kind == Scalars<T>::Exclude && rhs.kind == Scalars<T>::Exclude) {
    new_kind = Scalars<T>::Exclude;
    std::set_union(lhs.collection.begin(), lhs.collection.end(),
                   rhs.collection.begin(), rhs.collection.end(),
                   std::insert_iterator<std::set<T>>(new_collection,
                                                     new_collection.begin()));
  } else if (lhs.kind == Scalars<T>::Exclude &&
             rhs.kind == Scalars<T>::Include) {
    new_kind = Scalars<T>::Include;
    std::set_difference(rhs.collection.begin(), rhs.collection.end(),
                        lhs.collection.begin(), lhs.collection.end(),
                        std::insert_iterator<std::set<T>>(
                            new_collection, new_collection.begin()));
  } else if (lhs.kind == Scalars<T>::Include &&
             rhs.kind == Scalars<T>::Exclude) {
    new_kind = Scalars<T>::Include;
    std::set_difference(lhs.collection.begin(), lhs.collection.end(),
                        rhs.collection.begin(), rhs.collection.end(),
                        std::insert_iterator<std::set<T>>(
                            new_collection, new_collection.begin()));
  } else if (lhs.kind == Scalars<T>::Include &&
             rhs.kind == Scalars<T>::Include) {
    new_kind = Scalars<T>::Include;
    std::set_intersection(lhs.collection.begin(), lhs.collection.end(),
                          rhs.collection.begin(), rhs.collection.end(),
                          std::insert_iterator<std::set<T>>(
                              new_collection, new_collection.begin()));
  } else {
    return null_impl();
  }

  if (new_kind == Scalars<T>::Include && new_collection.size() == 0) {
    return theNothing;
  }
  return std::make_shared<Scalars<T>>(lhs.location, new_kind, new_collection);
}

Pattern::ref intersect(Pattern::ref lhs, Pattern::ref rhs) {
  /* ironically, this is where pattern matching would really help... */
  auto lhs_nothing = lhs->asNothing();
  auto rhs_nothing = rhs->asNothing();

  if (lhs_nothing || rhs_nothing) {
    /* intersection of nothing and anything is nothing */
    return theNothing;
  }

  auto lhs_allof = asAllOf(lhs);
  auto rhs_allof = asAllOf(rhs);

  if (lhs_allof) {
    /* intersection of everything and x is x */
    return rhs;
  }

  if (rhs_allof) {
    /* intersection of everything and x is x */
    return lhs;
  }

  auto lhs_ctor_pattern = asCtorPattern(lhs);
  auto rhs_ctor_pattern = asCtorPattern(rhs);

  if (lhs_ctor_pattern && rhs_ctor_pattern) {
    std::vector<CtorPatternValue> lhs_cpvs({lhs_ctor_pattern->cpv});
    std::vector<CtorPatternValue> rhs_cpvs({rhs_ctor_pattern->cpv});
    return intersect(rhs->location, lhs_cpvs, rhs_cpvs);
  }

  auto lhs_ctor_patterns = asCtorPatterns(lhs);
  auto rhs_ctor_patterns = asCtorPatterns(rhs);

  if (lhs_ctor_patterns && rhs_ctor_pattern) {
    std::vector<CtorPatternValue> rhs_cpvs({rhs_ctor_pattern->cpv});
    return intersect(rhs->location, lhs_ctor_patterns->cpvs, rhs_cpvs);
  }

  if (lhs_ctor_pattern && rhs_ctor_patterns) {
    std::vector<CtorPatternValue> lhs_cpvs({lhs_ctor_pattern->cpv});
    return intersect(rhs->location, lhs_cpvs, rhs_ctor_patterns->cpvs);
  }

  auto lhs_integers = asScalars<int64_t>(lhs);
  auto rhs_integers = asScalars<int64_t>(rhs);

  if (lhs_integers && rhs_integers) {
    return intersect(*lhs_integers, *rhs_integers);
  }

  auto lhs_floats = asScalars<double>(lhs);
  auto rhs_floats = asScalars<double>(rhs);

  if (lhs_floats && rhs_floats) {
    return intersect(*lhs_floats, *rhs_floats);
  }

  auto lhs_chars = asScalars<uint8_t>(lhs);
  auto rhs_chars = asScalars<uint8_t>(rhs);

  if (lhs_chars && rhs_chars) {
    return intersect(*lhs_chars, *rhs_chars);
  }

  log_location(log_error, lhs->location,
               "intersect is not implemented yet (%s vs. %s)",
               lhs->str().c_str(), rhs->str().c_str());

  throw zion::user_error(INTERNAL_LOC(), "not implemented");
  return nullptr;
}

Pattern::ref pattern_union(Pattern::ref lhs, Pattern::ref rhs) {
  auto lhs_nothing = lhs->asNothing();
  auto rhs_nothing = rhs->asNothing();

  if (lhs_nothing) {
    return rhs;
  }

  if (rhs_nothing) {
    return lhs;
  }

  auto lhs_ctor_patterns = asCtorPatterns(lhs);
  auto rhs_ctor_patterns = asCtorPatterns(rhs);

  if (lhs_ctor_patterns && rhs_ctor_patterns) {
    std::vector<CtorPatternValue> cpvs = lhs_ctor_patterns->cpvs;
    for (auto &cpv : rhs_ctor_patterns->cpvs) {
      cpvs.push_back(cpv);
    }
    return std::make_shared<CtorPatterns>(lhs->location, cpvs);
  }

  auto lhs_ctor_pattern = asCtorPattern(lhs);
  auto rhs_ctor_pattern = asCtorPattern(rhs);

  if (lhs_ctor_patterns && rhs_ctor_pattern) {
    std::vector<CtorPatternValue> cpvs;
    cpvs.reserve(lhs_ctor_patterns->cpvs.size() + 1);
    std::copy(lhs_ctor_patterns->cpvs.begin(), lhs_ctor_patterns->cpvs.end(),
              std::back_inserter(cpvs));
    cpvs.push_back(rhs_ctor_pattern->cpv);
    return std::make_shared<CtorPatterns>(lhs->location, cpvs);
  }

  if (lhs_ctor_pattern && rhs_ctor_patterns) {
    std::vector<CtorPatternValue> cpvs;
    cpvs.reserve(rhs_ctor_patterns->cpvs.size() + 1);
    std::copy(rhs_ctor_patterns->cpvs.begin(), rhs_ctor_patterns->cpvs.end(),
              std::back_inserter(cpvs));
    cpvs.push_back(lhs_ctor_pattern->cpv);
    return std::make_shared<CtorPatterns>(lhs->location, cpvs);
  }

  if (lhs_ctor_pattern && rhs_ctor_pattern) {
    std::vector<CtorPatternValue> cpvs{lhs_ctor_pattern->cpv,
                                       rhs_ctor_pattern->cpv};
    return std::make_shared<CtorPatterns>(lhs->location, cpvs);
  }

  log_location(log_error, lhs->location, "unhandled pattern_union (%s ∪ %s)",
               lhs->str().c_str(), rhs->str().c_str());
  throw zion::user_error(INTERNAL_LOC(), "not implemented");
  return nullptr;
}

Pattern::ref from_type(Location location,
                       const zion::DataCtorsMap &data_ctors_map,
                       types::Ref type) {
  if (auto tuple_type = dyncast<const types::TypeTuple>(type)) {
    std::vector<Pattern::ref> args;
    for (auto dim : tuple_type->dimensions) {
      args.push_back(from_type(location, data_ctors_map, dim));
    }
    CtorPatternValue cpv{type->repr(), "tuple", args};
    return std::make_shared<CtorPattern>(location, cpv);
  } else if (type_equality(type, type_int(INTERNAL_LOC()))) {
    return allIntegers;
  } else if (type_equality(type, type_id(make_iid(CHAR_TYPE)))) {
    return allChars;
  } else if (type_equality(type, type_id(make_iid(FLOAT_TYPE)))) {
    return allFloats;
  } else if (unify(type, type_ptr(type_variable(location))).result) {
    return all_of(location, {}, data_ctors_map, type);
  } else if (unify(type, type_arrow(type_params({type_variable(location)}),
                                    type_variable(location)))
                 .result) {
    return all_of(location, {}, data_ctors_map, type);
  } else {
    // TODO: support Char values here...
    auto ctors_types = zion::get_data_ctors_types(data_ctors_map, type);
    std::vector<CtorPatternValue> cpvs;

    for (auto pair : ctors_types) {
      auto &ctor_name = pair.first;
      auto ctor_terms = unfold_arrows(pair.second);

      std::vector<Pattern::ref> args;
      args.reserve(ctor_terms.size() - 1);

      for (size_t i = 0; i < ctor_terms.size() - 1; ++i) {
        args.push_back(std::make_shared<AllOf>(location, maybe<Identifier>(),
                                               data_ctors_map, ctor_terms[i]));
      }
      /* add a ctor */
      cpvs.push_back(CtorPatternValue{type->repr(), ctor_name, args});
    }

    if (cpvs.size() == 1) {
      return std::make_shared<CtorPattern>(location, cpvs[0]);
    } else if (cpvs.size() > 1) {
      return std::make_shared<CtorPatterns>(location, cpvs);
    } else {
      throw zion::user_error(INTERNAL_LOC(), "not implemented");
    }
  }

  /* just accept all of whatever this is */
  return std::make_shared<AllOf>(
      type->get_location(),
      maybe<Identifier>(
          Identifier{string_format("AllOf(%s)", type->str().c_str()),
                     type->get_location()}),
      data_ctors_map, type);
}

void difference(Pattern::ref lhs,
                Pattern::ref rhs,
                const std::function<void(Pattern::ref)> &send);

void difference(Location location,
                const CtorPatternValue &lhs,
                const CtorPatternValue &rhs,
                const std::function<void(Pattern::ref)> &send) {
  assert(lhs.type_name == rhs.type_name);

  if (lhs.name != rhs.name) {
    send(std::make_shared<CtorPattern>(location, lhs));
  } else if (lhs.args.size() == 0) {
    send(theNothing);
  } else {
    assert(lhs.args.size() == rhs.args.size());
    size_t i = 0;
    auto send_ctor_pattern = [location, &i, &lhs, &send](Pattern::ref arg) {
      if (dyncast<const Nothing>(arg)) {
        send(theNothing);
      } else {
        std::vector<Pattern::ref> args = lhs.args;
        args[i] = arg;
        send(std::make_shared<CtorPattern>(
            location, CtorPatternValue{lhs.type_name, lhs.name, args}));
      }
    };

    for (; i < lhs.args.size(); ++i) {
      difference(lhs.args[i], rhs.args[i], send_ctor_pattern);
    }
  }
}

template <typename T>
Pattern::ref difference(const Scalars<T> &lhs, const Scalars<T> &rhs) {
  typename Scalars<T>::Kind new_kind;
  std::set<T> new_collection;

  if (lhs.kind == Scalars<T>::Exclude && rhs.kind == Scalars<T>::Exclude) {
    new_kind = Scalars<T>::Include;
    std::set_difference(rhs.collection.begin(), rhs.collection.end(),
                        lhs.collection.begin(), lhs.collection.end(),
                        std::insert_iterator<std::set<T>>(
                            new_collection, new_collection.begin()));
  } else if (lhs.kind == Scalars<T>::Exclude &&
             rhs.kind == Scalars<T>::Include) {
    new_kind = Scalars<T>::Exclude;
    std::set_union(rhs.collection.begin(), rhs.collection.end(),
                   lhs.collection.begin(), lhs.collection.end(),
                   std::insert_iterator<std::set<T>>(new_collection,
                                                     new_collection.begin()));
  } else if (lhs.kind == Scalars<T>::Include &&
             rhs.kind == Scalars<T>::Exclude) {
    new_kind = Scalars<T>::Include;
    std::set_intersection(rhs.collection.begin(), rhs.collection.end(),
                          lhs.collection.begin(), lhs.collection.end(),
                          std::insert_iterator<std::set<T>>(
                              new_collection, new_collection.begin()));
  } else if (lhs.kind == Scalars<T>::Include &&
             rhs.kind == Scalars<T>::Include) {
    new_kind = Scalars<T>::Include;
    std::set_difference(lhs.collection.begin(), lhs.collection.end(),
                        rhs.collection.begin(), rhs.collection.end(),
                        std::insert_iterator<std::set<T>>(
                            new_collection, new_collection.begin()));
  } else {
    return null_impl();
  }

  if (new_kind == Scalars<T>::Include && new_collection.size() == 0) {
    return theNothing;
  }
  return std::make_shared<Scalars<T>>(lhs.location, new_kind, new_collection);
}

void difference(Pattern::ref lhs,
                Pattern::ref rhs,
                const std::function<void(Pattern::ref)> &send) {
  debug_above(8, log_location(rhs->location, "computing %s \\ %s",
                              lhs->str().c_str(), rhs->str().c_str()));

  auto lhs_nothing = lhs->asNothing();
  auto rhs_nothing = rhs->asNothing();
  auto lhs_allof = asAllOf(lhs);
  auto rhs_allof = asAllOf(rhs);
  auto lhs_ctor_patterns = asCtorPatterns(lhs);
  auto rhs_ctor_patterns = asCtorPatterns(rhs);
  auto lhs_ctor_pattern = asCtorPattern(lhs);
  auto rhs_ctor_pattern = asCtorPattern(rhs);
  auto lhs_integers = asScalars<int64_t>(lhs);
  auto rhs_integers = asScalars<int64_t>(rhs);
  auto lhs_floats = asScalars<double>(lhs);
  auto rhs_floats = asScalars<double>(rhs);
  auto lhs_chars = asScalars<uint8_t>(lhs);
  auto rhs_chars = asScalars<uint8_t>(rhs);

  if (lhs_nothing || rhs_nothing) {
    send(lhs);
    return;
  }

  if (lhs_allof) {
    if (rhs_allof) {
      if (lhs_allof->type->repr() == rhs_allof->type->repr()) {
        /* subtracting an entire type from itself */
        send(theNothing);
        return;
      }

      auto error = zion::user_error(
          lhs->location,
          "type mismatch when comparing ctors for pattern intersection");
      error.add_info(rhs->location, "comparing this type");
      throw error;
    }

    difference(
        from_type(lhs->location, lhs_allof->data_ctors_map, lhs_allof->type),
        rhs, send);
    return;
  }

  if (rhs_allof) {
    difference(
        lhs,
        from_type(rhs->location, rhs_allof->data_ctors_map, rhs_allof->type),
        send);
    return;
  }

  assert(lhs_allof == nullptr);
  assert(rhs_allof == nullptr);

  if (lhs_ctor_patterns) {
    if (rhs_ctor_patterns) {
      for (auto &cpv : lhs_ctor_patterns->cpvs) {
        difference(
            std::make_shared<CtorPattern>(lhs_ctor_patterns->location, cpv),
            rhs, send);
      }
      return;
    } else if (rhs_ctor_pattern) {
      for (auto &cpv : lhs_ctor_patterns->cpvs) {
        difference(lhs->location, cpv, rhs_ctor_pattern->cpv, send);
      }
      return;
    } else {
      throw zion::user_error(rhs->location, "type mismatch");
    }
  }

  if (lhs_ctor_pattern) {
    if (rhs_ctor_patterns) {
      Pattern::ref new_a = std::make_shared<CtorPattern>(lhs->location,
                                                         lhs_ctor_pattern->cpv);
      auto new_a_send = [&new_a](Pattern::ref pattern) {
        new_a = pattern_union(new_a, pattern);
      };

      for (auto &b : rhs_ctor_patterns->cpvs) {
        auto current_a = new_a;
        new_a = theNothing;
        difference(current_a, std::make_shared<CtorPattern>(rhs->location, b),
                   new_a_send);
      }

      send(new_a);
      return;
    } else if (rhs_ctor_pattern) {
      difference(lhs->location, lhs_ctor_pattern->cpv, rhs_ctor_pattern->cpv,
                 send);
      return;
    }
  }

  if (lhs_integers) {
    if (rhs_integers) {
      send(difference(*lhs_integers, *rhs_integers));
      return;
    }
  }

  if (lhs_floats) {
    if (rhs_floats) {
      send(difference(*lhs_floats, *rhs_floats));
      return;
    }
  }

  if (lhs_chars) {
    if (rhs_chars) {
      send(difference(*lhs_chars, *rhs_chars));
      return;
    }
  }

  log_location(log_error, lhs->location, "unhandled difference - %s \\ %s",
               lhs->str().c_str(), rhs->str().c_str());
  throw zion::user_error(INTERNAL_LOC(), "not implemented");
}

Pattern::ref difference(Pattern::ref lhs, Pattern::ref rhs) {
  Pattern::ref computed = theNothing;

  auto send = [&computed](Pattern::ref pattern) {
    computed = pattern_union(pattern, computed);
  };

  difference(lhs, rhs, send);

  return computed;
}

std::string AllOf::str() const {
  std::stringstream ss;
  if (name.valid) {
    ss << name.t;
  } else {
    ss << "_";
  }
  return ss.str();
}

std::string Nothing::str() const {
  return "∅";
}

std::string CtorPattern::str() const {
  return cpv.str();
}

std::string CtorPatterns::str() const {
  return ::join_with(
      cpvs, " and ",
      [](const CtorPatternValue &cpv) -> std::string { return cpv.str(); });
}

std::string CtorPatternValue::str() const {
  std::stringstream ss;
  ss << zion::tld::strip_prefix(name);
  if (args.size() != 0) {
    ss << "(" << ::join_str(args, ", ") << ")";
  }
  return ss.str();
}

} // namespace match

namespace zion {
namespace ast {
using namespace ::match;
using namespace ::types;

Pattern::ref TuplePredicate::get_pattern(
    types::Ref type,
    const zion::DataCtorsMap &data_ctors_map) const {
  std::vector<Pattern::ref> args;
  if (auto tuple_type = dyncast<const TypeTuple>(type)) {
    if (tuple_type->dimensions.size() != params.size()) {
      throw zion::user_error(location,
                             "tuple predicate has an incorrect number of "
                             "sub-patterns. there are %d, there should be %d",
                             int(params.size()),
                             int(tuple_type->dimensions.size()));
    }

    std::vector<Pattern::ref> args;
    for (size_t i = 0; i < params.size(); ++i) {
      args.push_back(
          params[i]->get_pattern(tuple_type->dimensions[i], data_ctors_map));
    }
    return std::make_shared<CtorPattern>(
        location, CtorPatternValue{tuple_type->repr(), "tuple", args});
  } else {
    throw zion::user_error(location,
                           "type mismatch on pattern. incoming type is %s. "
                           "it is not a %d-tuple.",
                           type->str().c_str(), (int)params.size());
    return nullptr;
  }
}

Pattern::ref CtorPredicate::get_pattern(
    types::Ref type,
    const zion::DataCtorsMap &data_ctors_map) const {
  auto ctor_terms = unfold_arrows(
      get_data_ctor_type(data_ctors_map, type, ctor_name));

  std::vector<Pattern::ref> args;
  if (ctor_terms.size() - 1 != params.size()) {
    log("params = %s", join_str(params).c_str());
    log("ctor_terms = %s", join_str(ctor_terms).c_str());
    throw zion::user_error(
        location,
        "%s has an incorrect number of sub-patterns. there are "
        "%d, there should be %d",
        ctor_name.name.c_str(), int(params.size()), int(ctor_terms.size() - 1));
  }

  for (size_t i = 0; i < params.size(); ++i) {
    args.push_back(params[i]->get_pattern(ctor_terms[i], data_ctors_map));
  }

  /* found the ctor we're matching on */
  return std::make_shared<CtorPattern>(
      location, CtorPatternValue{type->repr(), ctor_name.name, args});
}

Pattern::ref IrrefutablePredicate::get_pattern(
    types::Ref type,
    const zion::DataCtorsMap &data_ctors_map) const {
  return std::make_shared<AllOf>(location, name_assignment, data_ctors_map,
                                 type);
}

Pattern::ref Literal::get_pattern(
    types::Ref type,
    const zion::DataCtorsMap &data_ctors_map) const {
  if (type_equality(type, type_int(INTERNAL_LOC()))) {
    if (token.tk == tk_integer) {
      int64_t value = parse_int_value(token);
      return std::make_shared<Scalars<int64_t>>(
          token.location, Scalars<int64_t>::Include, std::set<int64_t>{value});
    } else if (token.tk == tk_identifier) {
      return std::make_shared<Scalars<int64_t>>(
          token.location, Scalars<int64_t>::Exclude, std::set<int64_t>{});
    }
  } else if (type_equality(type, type_id(make_iid(FLOAT_TYPE)))) {
    if (token.tk == tk_float) {
      double value = parse_float_value(token);
      return std::make_shared<Scalars<double>>(
          token.location, Scalars<double>::Include, std::set<double>{value});
    }
  } else if (type_equality(type, type_id(make_iid(CHAR_TYPE)))) {
    if (token.tk == tk_char) {
      uint8_t value = token.text[0];
      return std::make_shared<Scalars<uint8_t>>(
          token.location, Scalars<uint8_t>::Include, std::set<uint8_t>{value});
    }
  }

  throw zion::user_error(
      token.location, "invalid type for literal '%s' (%s). should be a %s",
      token.text.c_str(), tkstr(token.tk), type->str().c_str());
  return nullptr;
}
} // namespace ast
} // namespace zion
