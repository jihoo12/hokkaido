#include "lexer.h"

#include <cctype>

char Lexer::peek() {
  if (pos >= input.size()) return '\0';
  return input[pos];
}

char Lexer::advance() {
  char c = input[pos++];
  if (c == '\n') { line++; col = 1; }
  else { col++; }
  return c;
}

void Lexer::skip_whitespace() {
  while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\r'))
    advance();
}

void Lexer::skip_line_comment() {
  while (pos < input.size() && input[pos] != '\n') advance();
}

Token Lexer::next_token() {
  skip_whitespace();

  if (pos >= input.size()) return {TOK_EOF, "", 0, line, col};

  int l = line, c = col;
  char ch = peek();

  // Line comments
  if (ch == '/' && pos + 1 < input.size() && input[pos + 1] == '/') {
    skip_line_comment();
    return next_token();
  }

  // Newlines (statement separators)
  if (ch == '\n') {
    advance();
    return {TOK_NEWLINE, "\\n", 0, l, c};
  }

  // Semicolons
  if (ch == ';') {
    advance();
    return {TOK_SEMICOLON, ";", 0, l, c};
  }

  // Equals
  if (ch == '=') {
    advance();
    return {TOK_EQUALS, "=", 0, l, c};
  }

  // String literals
  if (ch == '"') {
    return lex_string(l, c);
  }

  // Numbers
  if (std::isdigit(ch) || (ch == '-' && pos + 1 < input.size() && std::isdigit(input[pos + 1]))) {
    return lex_number(l, c);
  }

  // Identifiers and keywords
  if (std::isalpha(ch) || ch == '_') {
    return lex_identifier(l, c);
  }

  // Unknown character
  std::string err = "unexpected character '";
  err += ch;
  err += "'";
  advance();
  return {TOK_EOF, err, 0, l, c};
}

Token Lexer::lex_string(int l, int c) {
  advance(); // skip opening "
  std::string val;
  while (pos < input.size() && peek() != '"') {
    if (peek() == '\\') {
      advance();
      if (peek() == 'n') val += '\n';
      else if (peek() == 't') val += '\t';
      else if (peek() == '"') val += '"';
      else if (peek() == '\\') val += '\\';
      else val += peek();
      advance();
    } else {
      val += advance();
    }
  }
  if (pos >= input.size()) {
    return {TOK_EOF, "unterminated string", 0, l, c};
  }
  advance(); // skip closing "
  return {TOK_STRING_LITERAL, val, 0, l, c};
}

Token Lexer::lex_number(int l, int c) {
  std::string num;
  bool is_float = false;
  if (peek() == '-') { num += advance(); }
  while (pos < input.size() && std::isdigit(peek())) {
    num += advance();
  }
  if (pos < input.size() && peek() == '.') {
    is_float = true;
    num += advance();
    while (pos < input.size() && std::isdigit(peek())) {
      num += advance();
    }
  }
  double val = std::stod(num);
  return {TOK_NUMBER, num, val, l, c};
}

Token Lexer::lex_identifier(int l, int c) {
  std::string id;
  while (pos < input.size() && (std::isalnum(peek()) || peek() == '_' || peek() == '-')) {
    id += advance();
  }

  // Check keywords
  if (id == "let") return {TOK_LET, id, 0, l, c};
  if (id == "cubical") return {TOK_CUBICAL, id, 0, l, c};
  if (id == "print") return {TOK_PRINT, id, 0, l, c};
  if (id == "int") return {TOK_INT, id, 0, l, c};
  if (id == "float") return {TOK_FLOAT, id, 0, l, c};
  if (id == "string") return {TOK_STRING, id, 0, l, c};

  return {TOK_IDENTIFIER, id, 0, l, c};
}