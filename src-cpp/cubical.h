#pragma once

#include <cstdint>
#include <string>

// =========================================================================
// Cubical FFI — link against the Rust cubical_c static library
// =========================================================================

extern "C" {
/// Evaluate a cubical source string. Returns a C string (caller must free
/// with cubical_free_string), or nullptr on error.
char *cubical_eval(const char *source);

/// Evaluate a cubical source string and return the result as a 64-bit
/// integer. The expression must evaluate to a natural number (Nat).
/// Returns -1 on error.
int64_t cubical_eval_int(const char *source);

/// Free a string returned by cubical_eval.
void cubical_free_string(char *s);
}

// =========================================================================
// Cubical C++ Wrapper
// =========================================================================

/// A compile-time evaluated cubical expression.
class cubical_value {
  std::string result_;
  bool valid_ = false;

public:
  explicit cubical_value(const std::string &source);

  bool valid() const { return valid_; }
  const std::string &str() const { return result_; }

  /// If the result is a natural number, return its value. Returns -1 if
  /// the result is not a Nat or evaluation failed.
  int64_t as_int() const;

private:
  int64_t parse_nat(const std::string &s) const;
};