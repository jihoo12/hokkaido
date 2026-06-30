#include "cubical.h"

cubical_value::cubical_value(const std::string &source) {
  char *res = cubical_eval(source.c_str());
  if (res) {
    result_ = res;
    valid_ = true;
    cubical_free_string(res);
  }
}

int64_t cubical_value::as_int() const {
  if (!valid_) return -1;
  auto eq_pos = result_.find(" = ");
  if (eq_pos == std::string::npos) return -1;
  std::string nat_str = result_.substr(eq_pos + 3);
  return parse_nat(nat_str);
}

int64_t cubical_value::parse_nat(const std::string &s) const {
  std::string t = s;
  while (!t.empty() && t.front() == ' ') t.erase(0, 1);
  while (!t.empty() && t.back() == ' ') t.pop_back();
  if (t.empty()) return -1;

  // Try decimal first (Nat may now be displayed as "253" instead of
  // "suc (suc ... zero)").
  try {
    size_t pos;
    int64_t val = std::stoll(t, &pos);
    if (pos == t.length() && val >= 0) {
      return val;
    }
  } catch (...) {}

  while (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
    t = t.substr(1, t.size() - 2);
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    while (!t.empty() && t.back() == ' ') t.pop_back();

    // After stripping parens, try decimal again (e.g. "(253)")
    try {
      size_t pos;
      int64_t val = std::stoll(t, &pos);
      if (pos == t.length() && val >= 0) {
        return val;
      }
    } catch (...) {}
  }

  if (t == "zero") return 0;
  if (t.size() >= 4 && t.substr(0, 4) == "suc ") {
    int64_t inner = parse_nat(t.substr(4));
    return (inner >= 0) ? inner + 1 : -1;
  }
  if (t.size() >= 4 && t.substr(0, 4) == "suc(") {
    int64_t inner = parse_nat("(" + t.substr(3));
    return (inner >= 0) ? inner + 1 : -1;
  }
  return -1;
}