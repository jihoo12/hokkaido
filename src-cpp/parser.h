#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ast.h"
#include "lexer.h"

// =========================================================================
// Hokkaido Language — Parser
// =========================================================================

class Parser {
  Lexer &lexer;
  Token cur_tok;
  bool has_error = false;
  std::string error_msg;

  void next_token();

public:
  Parser(Lexer &lex) : lexer(lex) { next_token(); }

  bool ok() const { return !has_error; }
  const std::string &error() const { return error_msg; }

  /// Parse a complete program: sequence of declarations.
  std::vector<std::unique_ptr<Decl>> parse_program();

private:
  std::unique_ptr<LetDecl> parse_let_decl();
  std::unique_ptr<PrintDecl> parse_print_decl();
};