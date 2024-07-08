#pragma once

#include <string>
#include <vector>

#include "identifier.h"

namespace ace {
namespace tld {

std::string mktld(std::string module, std::string name);
std::string tld(std::string name);
Identifier tld(Identifier id);
bool is_fqn(std::string name);
bool is_tld(std::string name);
std::vector<std::string> split_fqn(std::string fqn);
std::string fqn_leaf(std::string fqn);
bool is_tld_type(std::string name);
bool is_lowercase_leaf(std::string name);
bool is_in_module(std::string module, std::string name);
std::string strip_prefix(std::string fqn);

} // namespace tld
} // namespace ace
