#pragma once
#include <ostream>
#include <vector>

namespace cider {
namespace ast {
struct Expr;
struct Var;
struct Predicate;
struct PatternBlock;
typedef std::vector<const PatternBlock *> PatternBlocks;
struct Match;
struct TypeDecl;
struct TypeClass;
struct Block;
struct As;
struct Application;
struct Lambda;
struct Let;
struct Literal;
struct Conditional;
struct ReturnStatement;
struct While;
struct Decl;
struct Module;
struct Program;
} // namespace ast
} // namespace cider

std::ostream &operator<<(std::ostream &os, cider::ast::Program *program);
std::ostream &operator<<(std::ostream &os, cider::ast::Decl *decl);
