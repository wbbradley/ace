#pragma once

bool run_tests(std::string filter, std::vector<std::string> excludes);
std::vector<std::string> read_test_excludes();
void truncate_excludes();
