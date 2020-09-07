#include "testing.h"

#include <cstdio>
#include <iostream>

#include "colors.h"
#include "utils.h"

namespace zion {
namespace testing {

int run_test(std::string test_name) {
  std::stringstream ss;
  ss << "DEBUG= zion run " << test_name;
  auto pair = shell_get_output(ss.str());
  if (pair.first) {
    return EXIT_FAILURE;
  } else {
    return EXIT_SUCCESS;
  }
}

int run_tests(std::list<std::string> tests) {
  std::list<std::string> failures;
  std::list<std::string> passes;
  for (auto test : tests) {
    if (run_test(test)) {
      failures.push_back(test);
    } else {
      passes.push_back(test);
    }
  }
  for (auto pass : passes) {
    std::cout << "Test " c_good("passed") ": " << pass << std::endl;
  }
  for (auto failure : failures) {
    std::cout << "Test " c_error("failed") ": " << failure << std::endl;
  }
  return failures.size() ? EXIT_FAILURE : EXIT_SUCCESS;
}
} // namespace testing
} // namespace zion
