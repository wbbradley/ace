#pragma once
#include <ostream>
#include <vector>

namespace bitter {
struct Expr;
struct Var;
struct Predicate;
struct PatternBlock;
using pattern_blocks_t = std::vector<PatternBlock *>;
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
struct Fix;
struct Decl;
struct Module;
struct Program;
} // namespace bitter

std::ostream &operator<<(std::ostream &os, bitter::Program *program);
std::ostream &operator<<(std::ostream &os, bitter::Decl *decl);
