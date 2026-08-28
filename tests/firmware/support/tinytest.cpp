#include "tinytest.h"

#include <cstring>

namespace tinytest {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

int g_checks = 0;
int g_currentFailures = 0;
std::vector<std::string> g_failureLog;

void recordFailure(const std::string& detail, const char* file, int line) {
  ++g_currentFailures;

  // Trim to the basename; full Windows paths bury the point.
  const char* slash = strrchr(file, '/');
  const char* back = strrchr(file, '\\');
  const char* name = slash ? slash + 1 : (back ? back + 1 : file);

  g_failureLog.push_back(std::string(name) + ":" + std::to_string(line) +
                         "\n      " + detail);
}

int run(const std::string& filter) {
  int passed = 0;
  int failed = 0;
  std::string lastSuite;

  for (const TestCase& test : registry()) {
    const std::string full = test.suite + "." + test.name;
    if (!filter.empty() && full.find(filter) == std::string::npos) continue;

    if (test.suite != lastSuite) {
      std::cout << "\n  " << test.suite << "\n";
      lastSuite = test.suite;
    }

    g_currentFailures = 0;
    g_failureLog.clear();

    try {
      test.fn();
    } catch (const std::exception& exc) {
      recordFailure(std::string("threw: ") + exc.what(), __FILE__, __LINE__);
    } catch (...) {
      recordFailure("threw a non-standard exception", __FILE__, __LINE__);
    }

    if (g_currentFailures == 0) {
      ++passed;
      std::cout << "    ok   " << test.name << "\n";
    } else {
      ++failed;
      std::cout << "    FAIL " << test.name << "\n";
      for (const std::string& entry : g_failureLog) {
        std::cout << "      at " << entry << "\n";
      }
    }
  }

  std::cout << "\n" << passed << " passed";
  if (failed) std::cout << ", " << failed << " FAILED";
  std::cout << ", " << g_checks << " checks\n";

  return failed == 0 ? 0 : 1;
}

}  // namespace tinytest

int main(int argc, char** argv) {
  const std::string filter = (argc > 1) ? argv[1] : "";
  return tinytest::run(filter);
}
