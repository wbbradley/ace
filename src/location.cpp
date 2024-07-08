#include "location.h"

#include <sstream>
#include <string.h>
#include <string>
#include <unistd.h>

#include "dbg.h"
#include "utils.h"
#include "ace.h"

Location::Location() : Location({}, -1, -1) {
}

Location::Location(std::string filename, int line, int col)
    : filename(filename), line(line), col(col) {
}

std::string Location::filename_repr() const {
  static char *cwd = (char *)calloc(4096, 1);
  static unsigned cwdlen = 0;
  if (cwd[0] == 0) {
    if (getcwd(cwd, 4096) == cwd) {
      cwdlen = strlen(cwd);
    } else {
      panic("error when fussing with getcwd");
    }
  }

  std::stringstream ss;
  if (has_file_location()) {
    if (starts_with(filename, "./")) {
      auto str = filename.c_str();
      ss << (str + 2);
    } else {
      if (starts_with(filename, cwd) && filename.size() > cwdlen) {
        ss << filename.c_str() + strlen(cwd) + 1;
      } else {
        ss << filename;
      }
    }
  } else {
    ss << "builtin";
  }
  return ss.str();
}

std::string Location::str() const {
  std::stringstream ss;
  ss << C_LINE_REF << repr() << C_RESET;
  return ss.str();
}

std::string Location::repr() const {
  std::stringstream ss;
  ss << filename_repr() << ':' << line << ':' << col;
  return ss.str();
}

std::string Location::operator()() const {
  return str();
}

std::ostream &operator<<(std::ostream &os, const Location &location) {
  return os << location.str();
}

bool Location::operator<(const Location &rhs) const {
  if (filename < rhs.filename) {
    return true;
  } else if (filename > rhs.filename) {
    return false;
  } else if (line < rhs.line) {
    return true;
  } else if (line > rhs.line) {
    return false;
  } else {
    return col < rhs.col;
  }
}

bool Location::operator==(const Location &rhs) const {
  return filename == rhs.filename && line == rhs.line && col == rhs.col;
}

bool Location::operator!=(const Location &rhs) const {
  return filename != rhs.filename || line != rhs.line || col != rhs.col;
}

bool Location::has_file_location() const {
  return filename.size() != 0 && line != -1 && col != -1;
}

Location best_location(Location a, Location b) {
  /* this function is entirely heuristic garbage. */
  // FUTURE: do better at plumbing info around so that heuristics like this are
  // less necessary
  if (a.filename.find(".cpp") != std::string::npos) {
    return b;
  } else {
    if (a.filename.find("lib/") != std::string::npos &&
        b.filename.find("lib/") == std::string::npos) {
      return b;
    } else {
      return a;
    }
  }
}
