#pragma once
#include <string>
#include <map>
#include <set>
#include <vector>

void dump_atoms();
int atomize(std::string &&str);
int atomize(const std::string &str);
int atomize(const char *str);
