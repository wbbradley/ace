#pragma once

#include <string>
#include <vector>

namespace types {

struct type_t;

struct ClassPredicate final {
  typedef std::shared_ptr<const ClassPredicate> Ref;

  ClassPredicate() = delete;
  ClassPredicate(const ClassPredicate &) = delete;
  ClassPredicate(std::string classname, const std::vector<std::string> &tvs);

  std::string repr() const;
  std::string str() const;

  std::string const classname;
  std::vector<std::shared_ptr<const type_t>> const tvs;

  bool operator<(const ClassPredicate &rhs) const;

private:
  mutable bool has_repr_;
  mutable std::string repr_;
};

} // namespace types
