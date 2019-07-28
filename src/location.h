#pragma once

#include <ostream>

#include "utils.h"

#define INTERNAL_LOC()                                                         \
  ::Location {                                                                 \
    __FILE__, __LINE__, 1                                                      \
  }

struct Location {
  template <typename T> Location(T t) = delete;

  Location();
  explicit Location(std::string filename, int line, int col);

  std::string str() const;
  std::string repr() const;
  std::string operator()() const;
  std::string filename_repr() const;

  std::string filename;
  int line = -1;
  int col = -1;

  bool has_file_location() const;
  bool operator<(const Location &rhs) const;
  bool operator==(const Location &rhs) const;
  bool operator!=(const Location &rhs) const;
};

std::ostream &operator<<(std::ostream &os, const Location &location);
Location best_location(Location a, Location b);
