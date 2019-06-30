#include <map>
#include <set>
#include <string>

#include "ast.h"
#include "identifier.h"

std::string prefix(const std::set<std::string> &bindings,
                   std::string pre,
                   std::string name);
Identifier prefix(const std::set<std::string> &bindings,
                  std::string pre,
                  Identifier name);
Token prefix(const std::set<std::string> &bindings,
             std::string pre,
             Token name);
bitter::Expr *prefix(const std::set<std::string> &bindings,
                     std::string pre,
                     bitter::Expr *value);
bitter::Predicate *prefix(const std::set<std::string> &bindings,
                          std::string pre,
                          bitter::Predicate *predicate,
                          std::set<std::string> &new_symbols);
bitter::PatternBlock *prefix(const std::set<std::string> &bindings,
                             std::string pre,
                             bitter::PatternBlock *pattern_block);
bitter::Decl *prefix(const std::set<std::string> &bindings,
                     std::string pre,
                     bitter::Decl *value);
bitter::TypeDecl prefix(const std::set<std::string> &bindings,
                        std::string pre,
                        const bitter::TypeDecl &type_decl);
bitter::TypeClass *prefix(const std::set<std::string> &bindings,
                          std::string pre,
                          bitter::TypeClass *type_class);
types::ClassPredicateRef prefix(
    const std::set<std::string> &bindings,
    std::string pre,
    const types::ClassPredicateRef &class_predicate);
types::ClassPredicates prefix(const std::set<std::string> &bindings,
                              std::string pre,
                              const types::ClassPredicates &class_predicates);
types::Ref prefix(const std::set<std::string> &bindings,
                  std::string pre,
                  types::Ref type);
types::Scheme::Ref prefix(const std::set<std::string> &bindings,
                          std::string pre,
                          types::Scheme::Ref scheme);
bitter::Expr *prefix(const std::set<std::string> &bindings,
                     std::string pre,
                     bitter::Expr *value);
std::vector<bitter::Expr *> prefix(const std::set<std::string> &bindings,
                                   std::string pre,
                                   std::vector<bitter::Expr *> values);
bitter::Module *prefix(const std::set<std::string> &bindings,
                       bitter::Module *module);
bitter::Instance *prefix(const std::set<std::string> &bindings,
                         std::string pre,
                         bitter::Instance *instance);
DataCtorsMap prefix(const std::set<std::string> &bindings,
                    std::string pre,
                    const DataCtorsMap &data_ctors_map);
inline int prefix(const std::set<std::string> &, std::string, int x) {
  return x;
}

template <typename T>
std::vector<T> prefix(const std::set<std::string> &bindings,
                      std::string pre,
                      const std::vector<T> &things) {
  std::vector<T> new_things;
  for (T pb : things) {
    new_things.push_back(::prefix(bindings, pre, pb));
  }
  return new_things;
}

template <typename T>
std::set<T> prefix(const std::set<std::string> &bindings,
                   std::string pre,
                   const std::set<T> &set) {
  std::set<T> new_set;
  for (auto s : set) {
    new_set.insert(prefix(bindings, pre, s));
  }
  return new_set;
}

template <typename T>
std::map<std::string, T> prefix(const std::set<std::string> &bindings,
                                std::string pre,
                                const std::map<std::string, T> &map,
                                bool include_keys) {
  std::map<std::string, T> new_map;
  for (auto pair : map) {
    if (include_keys) {
      new_map[prefix(bindings, pre, pair.first)] = prefix(bindings, pre,
                                                          pair.second);
    } else {
      new_map[pair.first] = prefix(bindings, pre, pair.second);
    }
  }
  return new_map;
}

template <typename T>
std::unordered_map<std::string, T> prefix(
    const std::set<std::string> &bindings,
    std::string pre,
    const std::unordered_map<std::string, T> &map,
    bool include_keys) {
  std::unordered_map<std::string, T> new_map;
  for (auto pair : map) {
    if (include_keys) {
      new_map[prefix(bindings, pre, pair.first)] = prefix(bindings, pre,
                                                          pair.second);
    } else {
      new_map[pair.first] = prefix(bindings, pre, pair.second);
    }
  }
  return new_map;
}
