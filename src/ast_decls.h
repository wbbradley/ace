#pragma once
#include <ostream>
#include <vector>

namespace zion {
namespace bitter {
struct Expr;
struct Var;
struct Predicate;
struct PatternBlock;
typedef std::vector<const PatternBlock *> pattern_blocks_t;
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
} // namespace bitter
} // namespace zion

std::ostream &operator<<(std::ostream &os, zion::bitter::Program *program);
std::ostream &operator<<(std::ostream &os, zion::bitter::Decl *decl);
