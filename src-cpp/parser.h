#pragma once

#include <memory>
#include <set>
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

  // Directory the current source file lives in; used to resolve relative
  // `include "..."` paths.
  std::string base_dir;
  // Set of canonical file paths already included, shared across the whole
  // include tree, so a file (directly or transitively) cannot include
  // itself and get stuck in infinite recursion.
  std::shared_ptr<std::set<std::string>> included_files;

  void next_token();
  void skip_newlines();
  void set_error(const std::string &msg);

public:
  Parser(Lexer &lex, std::string base_dir = "",
         std::shared_ptr<std::set<std::string>> included_files = nullptr)
      : lexer(lex), base_dir(std::move(base_dir)),
        included_files(included_files ? std::move(included_files)
                                       : std::make_shared<std::set<std::string>>()) {
    next_token();
  }

  bool ok() const { return !has_error; }
  const std::string &error() const { return error_msg; }

  std::vector<std::unique_ptr<Decl>> parse_program();

private:
  std::unique_ptr<LetDecl> parse_let_decl();
  std::unique_ptr<FnDecl> parse_fn_decl();
  std::unique_ptr<FnDecl> parse_extern_fn_decl();
  std::unique_ptr<StructDecl> parse_struct_decl();
  bool parse_include_decl(std::vector<std::unique_ptr<Decl>> &decls);
  bool parse_namespace_decl(std::vector<std::unique_ptr<Decl>> &decls);
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
  std::unique_ptr<Expr> parse_postfix(std::unique_ptr<Expr> left);
  std::unique_ptr<Expr> parse_call_rest(const std::string &name);
  std::unique_ptr<Expr> parse_array_literal();

  // Shared let helper
  bool parse_let_common(TypeAnnotation &ann, std::string &name,
                        std::unique_ptr<Expr> &init);
};