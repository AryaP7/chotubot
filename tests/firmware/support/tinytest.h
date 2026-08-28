// A ~100 line test harness.
//
// Deliberately not a dependency: doctest or Catch2 would
// mean vendoring thousands of lines to get assertion
// macros and a runner, which is all this needs to do.

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tinytest {

struct TestCase {
  std::string suite;
  std::string name;
  std::function<void()> fn;
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* suite, const char* name, std::function<void()> fn) {
    registry().push_back({suite, name, std::move(fn)});
  }
};

extern int g_checks;
extern int g_currentFailures;
extern std::vector<std::string> g_failureLog;

void recordFailure(const std::string& detail, const char* file, int line);

template <typename T>
std::string show(const T& value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

// Chars print as numbers rather than glyphs; byte values
// are what these tests actually compare.
inline std::string show(uint8_t value) { return std::to_string(value); }
inline std::string show(int8_t value) { return std::to_string(value); }
inline std::string show(bool value) { return value ? "true" : "false"; }
inline std::string show(const char* value) {
  return value ? std::string("\"") + value + "\"" : "nullptr";
}

int run(const std::string& filter);

}  // namespace tinytest

#define TEST(suite_name, test_name)                                          \
  static void suite_name##_##test_name##_body();                             \
  static tinytest::Registrar suite_name##_##test_name##_reg(                 \
      #suite_name, #test_name, suite_name##_##test_name##_body);             \
  static void suite_name##_##test_name##_body()

#define CHECK(expr)                                                          \
  do {                                                                       \
    ++tinytest::g_checks;                                                    \
    if (!(expr)) {                                                           \
      tinytest::recordFailure(std::string("CHECK(") + #expr + ") is false",  \
                              __FILE__, __LINE__);                           \
    }                                                                        \
  } while (0)

#define CHECK_EQ(actual, expected)                                           \
  do {                                                                       \
    ++tinytest::g_checks;                                                    \
    auto&& _a = (actual);                                                    \
    auto&& _e = (expected);                                                  \
    if (!(_a == _e)) {                                                       \
      tinytest::recordFailure(                                               \
          std::string(#actual) + " == " + #expected + "\n      actual:   " + \
              tinytest::show(_a) + "\n      expected: " + tinytest::show(_e), \
          __FILE__, __LINE__);                                               \
    }                                                                        \
  } while (0)

#define CHECK_NE(actual, unexpected)                                         \
  do {                                                                       \
    ++tinytest::g_checks;                                                    \
    auto&& _a = (actual);                                                    \
    auto&& _u = (unexpected);                                                \
    if (_a == _u) {                                                          \
      tinytest::recordFailure(std::string(#actual) + " != " + #unexpected +  \
                                  "\n      both were: " + tinytest::show(_a), \
                              __FILE__, __LINE__);                           \
    }                                                                        \
  } while (0)

#define CHECK_STR_EQ(actual, expected)                                       \
  do {                                                                       \
    ++tinytest::g_checks;                                                    \
    const char* _a = (actual);                                               \
    const char* _e = (expected);                                             \
    if (_a == nullptr || _e == nullptr || strcmp(_a, _e) != 0) {             \
      tinytest::recordFailure(                                               \
          std::string(#actual) + "\n      actual:   " + tinytest::show(_a) + \
              "\n      expected: " + tinytest::show(_e),                     \
          __FILE__, __LINE__);                                               \
    }                                                                        \
  } while (0)
