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
  void skip_newlines();
  void set_error(const std::string &msg);

public:
  Parser(Lexer &lex) : lexer(lex) { next_token(); }

  bool ok() const { return !has_error; }
  const std::string &error() const { return error_msg; }

  std::vector<std::unique_ptr<Decl>> parse_program();

private:
  std::unique_ptr<LetDecl> parse_let_decl();
  std::unique_ptr<FnDecl> parse_fn_decl();
  TypeAnnotation parse_type_annotation();

  // Statements
  std::vector<std::unique_ptr<Stmt>> parse_block();
  std::unique_ptr<Stmt> parse_stmt();
  std::unique_ptr<LetStmt> parse_let_stmt();
  std::unique_ptr<ReturnStmt> parse_return_stmt();
  std::unique_ptr<IfStmt> parse_if_stmt();
  std::unique_ptr<ForStmt> parse_for_stmt();

  // Expressions
  std::unique_ptr<Expr> parse_expr();
  std::unique_ptr<Expr> parse_assignment();
  std::unique_ptr<Expr> parse_comparison();
  std::unique_ptr<Expr> parse_additive();
  std::unique_ptr<Expr> parse_multiplicative();
  std::unique_ptr<Expr> parse_unary();
  std::unique_ptr<Expr> parse_primary();
  std::unique_ptr<Expr> parse_call_rest(const std::string &name);

  // Shared let helper
  bool parse_let_common(TypeAnnotation &ann, std::string &name,
                        std::unique_ptr<Expr> &init);
};