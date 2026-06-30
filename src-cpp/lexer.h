#pragma once

#include <string>

// =========================================================================
// Hokkaido Language — Lexer
// =========================================================================

enum class TokenType {
  Eof,
  Let, Fn, Return,
  Cubical,
  Int, Float, String,
  Identifier,
  Number,
  StringLiteral,
  Equals, Arrow,
  Semicolon, Comma,
  Newline,
  LParen, RParen, LBrace, RBrace,
  Plus, Minus, Star, Slash,
};

struct Token {
  TokenType type;
  std::string text;
  double num_val = 0;
  int line = 0;
  int col = 0;
};

class Lexer {
  std::string input;
  size_t pos = 0;
  int line = 1;
  int col = 1;

  char peek();
  char advance();
  void skip_whitespace();
  void skip_line_comment();

public:
  Lexer(const std::string &src) : input(src) {}

  Token next_token();

private:
  Token lex_string(int l, int c);
  Token lex_number(int l, int c);
  Token lex_identifier(int l, int c);
};