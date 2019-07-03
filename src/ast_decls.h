#pragma once
#include <ostream>
#include <vector>

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

std::ostream &operator<<(std::ostream &os, bitter::Program *program);
std::ostream &operator<<(std::ostream &os, bitter::Decl *decl);
