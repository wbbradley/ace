#include <map>
#include <set>
#include <string>

#include "ast.h"
#include "identifier.h"

namespace zion {

std::string prefix(const std::set<std::string> &bindings,
                   std::string pre,
                   std::string name);
Identifier prefix(const std::set<std::string> &bindings,
                  std::string pre,
                  const Identifier &name);
Token prefix(const std::set<std::string> &bindings,
             std::string pre,
             Token name);
const ast::Expr *prefix(const std::set<std::string> &bindings,
                        std::string pre,
                        const ast::Expr *value);
const ast::Predicate *prefix(const std::set<std::string> &bindings,
                             std::string pre,
                             const ast::Predicate *predicate,
                             std::set<std::string> &new_symbols);
const ast::PatternBlock *prefix(const std::set<std::string> &bindings,
                                std::string pre,
                                const ast::PatternBlock *pattern_block);
const ast::Decl *prefix(const std::set<std::string> &bindings,
                        std::string pre,
                        const ast::Decl *value);
const ast::TypeDecl *prefix(const std::set<std::string> &bindings,
                            std::string pre,
                            const ast::TypeDecl *type_decl);
const ast::TypeClass *prefix(const std::set<std::string> &bindings,
                             std::string pre,
                             const ast::TypeClass *type_class);
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
const ast::Expr *prefix(const std::set<std::string> &bindings,
                        std::string pre,
                        const ast::Expr *value);
std::vector<ast::Expr *> prefix(const std::set<std::string> &bindings,
                                std::string pre,
                                std::vector<ast::Expr *> values);
const ast::Module *prefix(const std::set<std::string> &bindings,
                          const ast::Module *module);
const ast::Instance *prefix(const std::set<std::string> &bindings,
                            std::string pre,
                            const ast::Instance *instance);
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
    new_things.push_back(prefix(bindings, pre, pb));
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

} // namespace zion
