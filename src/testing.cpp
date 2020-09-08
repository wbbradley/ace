#include "testing.h"

#include <cstdio>
#include <iostream>
#include <regex>
#include <thread>
#ifdef linux
#include <bits/std_mutex.h>
#endif

#include "colors.h"
#include "dbg.h"
#include "disk.h"
#include "location.h"
#include "tarjan.h"
#include "tld.h"
#include "user_error.h"
#include "utils.h"

namespace zion {
namespace testing {

struct TestFlag {
  virtual ~TestFlag() {
  }
  virtual std::string emit_env_vars() const {
    return "";
  }
  virtual bool check_retcode(int ret) const {
    return true;
  }
  virtual bool check_output(const std::string &output) const {
    return true;
  }
  virtual bool should_skip_test() const {
    return false;
  }
  virtual Location get_location() const = 0;
  virtual std::string str() const = 0;
};

struct TestFlagExitStatus : public TestFlag {
  TestFlagExitStatus(Location location, bool succeed)
      : location(location), succeed(succeed) {
  }
  virtual ~TestFlagExitStatus() {
  }
  std::string str() const override {
    return string_format("test: %s", succeed ? "pass" : "fail");
  }
  bool check_retcode(int ret) const override {
    return succeed == bool(ret == 0);
  }
  Location get_location() const override {
    return location;
  }
  Location location;
  bool succeed;
};

struct TestFlagSkip : public TestFlag {
  TestFlagSkip(Location location) : location(location) {
  }
  virtual ~TestFlagSkip() {
  }
  std::string str() const override {
    return "test: skip";
  }
  bool should_skip_test() const override {
    return true;
  }
  Location get_location() const override {
    return location;
  }
  Location location;
  bool succeed;
};

struct TestFlagNoPrelude : public TestFlag {
  TestFlagNoPrelude(Location location) : location(location) {
  }
  virtual ~TestFlagNoPrelude() {
  }
  std::string emit_env_vars() const override {
    return "NO_PRELUDE=1";
  }
  std::string str() const override {
    return string_format("test: noprelude");
  }
  Location get_location() const override {
    return location;
  }
  Location location;
};

struct TestFlagExpectReject : public TestFlag {
  TestFlagExpectReject(Location location, bool expect, std::string regex)
      : location(location), expect(expect), regex(regex) {
  }
  virtual ~TestFlagExpectReject() {
  }
  bool check_output(const std::string &output) const override {
    return expect == regex_exists(output, regex);
  }
  std::string str() const override {
    return string_format("%s: " c_id("%s"), expect ? "expect" : "reject",
                         regex.c_str());
  }
  Location get_location() const override {
    return location;
  }
  Location location;
  bool expect;
  std::string regex;
};

std::list<std::unique_ptr<TestFlag>> get_test_flags(
    const std::string &filename,
    const std::vector<std::string> &lines) {
  std::list<std::unique_ptr<TestFlag>> test_flags;
  std::string matching;
  int lineno = 0;
  bool found_expects = false, found_fail = false;
  Location pass_location{filename, 1, 1};
  for (auto &line : lines) {
    ++lineno;
    std::string match;
    if (regex_lift_match(line, ".*# test: (.*)", match)) {
      for (auto flag : split(match, ",")) {
        trim(flag);
        if (flag == "pass") {
          pass_location = Location{filename, lineno, 1};
          test_flags.push_back(
              std::make_unique<TestFlagExitStatus>(pass_location, true));
        } else if (flag == "fail") {
          test_flags.push_back(std::make_unique<TestFlagExitStatus>(
              Location{filename, lineno, 1}, false));
          found_fail = true;
        } else if (flag == "skip") {
          test_flags.push_back(
              std::make_unique<TestFlagSkip>(Location{filename, lineno, 1}));
        } else if (flag == "noprelude") {
          test_flags.push_back(std::make_unique<TestFlagNoPrelude>(
              Location{filename, lineno, 1}));
        } else {
          throw user_error(Location{filename, lineno, 1},
                           "invalid test directive (%s)", flag.c_str());
        }
      }
    } else if (regex_lift_match(line, ".*# expect: (.*)", match)) {
      trim(match);
      test_flags.push_back(std::make_unique<TestFlagExpectReject>(
          Location{filename, lineno, 1}, true, match));
      found_expects = true;
    } else if (regex_lift_match(line, ".*# reject: (.*)", match)) {
      trim(match);
      test_flags.push_back(std::make_unique<TestFlagExpectReject>(
          Location{filename, lineno, 1}, false, match));
    }
  }
  if (!found_expects && !found_fail) {
    /* default test output is that we expect "PASS" */
    test_flags.push_back(
        std::make_unique<TestFlagExpectReject>(pass_location, true, "PASS"));
  }
  return test_flags;
}

enum RunTestResult {
  rtr_pass,
  rtr_fail,
  rtr_skip,
};

RunTestResult run_test(std::string test_name) {
  std::stringstream ss;
  ss << "DEBUG= zion run " << test_name;
  std::string command_line = ss.str();

  std::vector<std::string> lines = readlines(test_name);
  std::list<std::unique_ptr<TestFlag>> test_flags;
  try {
    test_flags = get_test_flags(test_name, lines);
  } catch (user_error &e) {
    print_exception(e);
    /* and continue */
    return rtr_fail;
  }

  for (auto &test_flag : test_flags) {
    if (test_flag->should_skip_test()) {
      return rtr_skip;
    }
  }
  for (auto &test_flag : test_flags) {
    auto env_vars = test_flag->emit_env_vars();
    trim(env_vars);
    if (env_vars.size() != 0) {
      command_line = env_vars + " " + command_line;
    }
  }
  auto pair = shell_get_output(command_line, true /*redirect_to_stdout*/);
  for (auto &test_flag : test_flags) {
    if (!test_flag->check_retcode(pair.first)) {
      log_location(log_error, test_flag->get_location(),
                   c_error("%s") " failed", test_flag->str().c_str());
      log(log_info, "%s", pair.second.c_str());
      return rtr_fail;
    }
  }
  for (auto &test_flag : test_flags) {
    if (!test_flag->check_output(pair.second)) {
      log_location(log_error, test_flag->get_location(),
                   c_error("%s") " failed", test_flag->str().c_str());
      return rtr_fail;
    } else {
      log_location(log_info, test_flag->get_location(), c_good("%s") " passed",
                   test_flag->str().c_str());
    }
  }
  return rtr_pass;
}

struct TestState {
  TestState(const std::list<std::string> &tests) : tests(tests) {
  }
  std::mutex mutex;
  std::list<std::string> tests;
  std::list<std::string> failures;
  std::list<std::string> passes;
  std::list<std::string> skips;
};

void run_test_thread(TestState *test_state) {
  while (true) {
    std::string test;
    if (true) {
      std::lock_guard<decltype(test_state->mutex)> lock(test_state->mutex);
      if (test_state->tests.size() != 0) {
        test = test_state->tests.front();
        test_state->tests.pop_front();
      } else {
        return;
      }
    }

    /* run the test outside of the mutex */
    RunTestResult rtr = run_test(test);

    std::lock_guard<decltype(test_state->mutex)> lock(test_state->mutex);

    switch (rtr) {
    case rtr_pass:
      test_state->passes.push_back(test);
      break;
    case rtr_skip:
      test_state->skips.push_back(test);
      break;
    case rtr_fail:
      test_state->failures.push_back(test);
      // Quit the tests upon failure.
      test_state->tests.clear();
      return;
    }
  }
}

int run_tests(std::list<std::string> tests) {
  std::unique_ptr<TestState> test_state = std::make_unique<TestState>(tests);
  const int NPROCS = 8;
  std::list<std::unique_ptr<std::thread>> threads;
  for (int i = 0; i < NPROCS; ++i) {
    threads.push_back(
        std::make_unique<std::thread>(run_test_thread, test_state.get()));
  }

  for (auto &thread : threads) {
    thread->join();
  }

  for (auto pass : test_state->passes) {
    std::cout << "Test " c_good("passed") ": " << pass << std::endl;
  }
  for (auto skip : test_state->skips) {
    std::cout << "Test " C_WARN "skipped" C_RESET ": " << skip << std::endl;
  }
  for (auto failure : test_state->failures) {
    std::cout << "Test " c_error("failed") ": " << failure << std::endl;
  }
  return test_state->failures.size() ? EXIT_FAILURE : EXIT_SUCCESS;
}

int run_unit_tests() {
  test_assert(alphabetize(0) == "a");
  test_assert(alphabetize(1) == "b");
  test_assert(alphabetize(2) == "c");
  test_assert(alphabetize(26) == "aa");
  test_assert(alphabetize(27) == "ab");

  tarjan::Graph graph;
  graph.insert({"a", {"b", "f"}});
  graph.insert({"b", {"c"}});
  graph.insert({"g", {"c", "f"}});
  graph.insert({"d", {"c"}});
  graph.insert({"c", {"d"}});
  graph.insert({"h", {"g"}});
  graph.insert({"f", {"h", "c"}});
  tarjan::SCCs sccs = tarjan::compute_strongly_connected_components(graph);
  auto sccs_str = str(sccs);
  std::string tarjan_expect = "{{c, d}, {f, g, h}, {b}, {a}}";
  if (sccs_str != tarjan_expect) {
    log("tarjan says: %s\nit should say: %s", sccs_str.c_str(),
        tarjan_expect.c_str());
    test_assert(false);
  }

  test_assert(zion::tld::split_fqn("::copy::Copy").size() == 2);
  test_assert(zion::tld::is_tld_type("::Copy"));
  test_assert(zion::tld::is_tld_type("::Z"));
  test_assert(!zion::tld::is_tld_type("::copy::copy"));
  test_assert(!zion::tld::is_tld_type("copy::copy"));
  test_assert(!zion::tld::is_tld_type("copy"));
  test_assert(zion::tld::is_tld_type("::copy::Copy"));
  test_assert(!zion::tld::is_tld_type("::copy::copy"));
  test_assert(tld::split_fqn("::inc").size() == 1);
  auto pair = shell_get_output("seq 10000");
  if (pair.first) {
    std::cout << pair.second << std::endl;
    std::cout << pair.second.size() << std::endl;
  }
  test_assert(!pair.first);
  test_assert(pair.second.find("10000") != std::string::npos);
  std::string output =
      "tests/test_assert_fail.zion:5:17: assertion failed: (std::False)\n";

  test_assert(regex_exists(output, "assertion failed.*False"));
  return EXIT_SUCCESS;
}

} // namespace testing
} // namespace zion
