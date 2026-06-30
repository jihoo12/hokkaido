#pragma once

#include <string>

// =========================================================================
// Hokkaido Language — Lexer
// =========================================================================

enum TokenType {
  TOK_EOF = -1,
  TOK_LET = -2,
  TOK_CUBICAL = -3,
  TOK_PRINT = -4,
  TOK_INT = -5,
  TOK_FLOAT = -6,
  TOK_STRING = -7,
  TOK_IDENTIFIER = -8,
  TOK_NUMBER = -9,
  TOK_STRING_LITERAL = -10,
  TOK_EQUALS = -11,
  TOK_SEMICOLON = -12,
  TOK_NEWLINE = -13,
};

struct Token {
  TokenType type;
  std::string text;
  double num_val;
  int line;
  int col;
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