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

  if (pos >= input.size()) return {TokenType::Eof, "", 0, line, col};

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
    return {TokenType::Newline, "\\n", 0, l, c};
  }

  // Single-character tokens
  if (ch == ';') { advance(); return {TokenType::Semicolon, ";", 0, l, c}; }
  if (ch == '=') { advance(); return {TokenType::Equals, "=", 0, l, c}; }
  if (ch == '(') { advance(); return {TokenType::LParen, "(", 0, l, c}; }
  if (ch == ')') { advance(); return {TokenType::RParen, ")", 0, l, c}; }
  if (ch == '{') { advance(); return {TokenType::LBrace, "{", 0, l, c}; }
  if (ch == '}') { advance(); return {TokenType::RBrace, "}", 0, l, c}; }
  if (ch == ',') { advance(); return {TokenType::Comma, ",", 0, l, c}; }
  if (ch == '+') { advance(); return {TokenType::Plus, "+", 0, l, c}; }
  if (ch == '*') { advance(); return {TokenType::Star, "*", 0, l, c}; }
  if (ch == '/') { advance(); return {TokenType::Slash, "/", 0, l, c}; }

  // Arrow and minus
  if (ch == '-') {
    if (pos + 1 < input.size() && input[pos + 1] == '>') {
      advance(); advance();
      return {TokenType::Arrow, "->", 0, l, c};
    }
    advance();
    return {TokenType::Minus, "-", 0, l, c};
  }

  // String literals
  if (ch == '"') {
    return lex_string(l, c);
  }

  // Numbers (including negative literals)
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
  return {TokenType::Eof, err, 0, l, c};
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
    return {TokenType::Eof, "unterminated string", 0, l, c};
  }
  advance(); // skip closing "
  return {TokenType::StringLiteral, val, 0, l, c};
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
  return {TokenType::Number, num, val, l, c};
}

Token Lexer::lex_identifier(int l, int c) {
  std::string id;
  while (pos < input.size() && (std::isalnum(peek()) || peek() == '_')) {
    id += advance();
  }

  if (id == "let") return {TokenType::Let, id, 0, l, c};
  if (id == "fn") return {TokenType::Fn, id, 0, l, c};
  if (id == "return") return {TokenType::Return, id, 0, l, c};
  if (id == "cubical") return {TokenType::Cubical, id, 0, l, c};
  if (id == "int") return {TokenType::Int, id, 0, l, c};
  if (id == "float") return {TokenType::Float, id, 0, l, c};
  if (id == "string") return {TokenType::String, id, 0, l, c};

  return {TokenType::Identifier, id, 0, l, c};
}