#pragma once

#include <memory>
#include <string>
#include <vector>

// =========================================================================
// Hokkaido Language — AST
// =========================================================================

enum class TypeKind {
  Int,
  Float,
  String,
  Cubical,
};

struct TypeAnnotation {
  TypeKind kind;
};

enum class BinOp {
  Add,
  Sub,
  Mul,
  Div,
};

struct Expr {
  virtual ~Expr() = default;
};

struct NumberExpr : Expr {
  double value;
  NumberExpr(double v) : value(v) {}
};

struct StringExpr : Expr {
  std::string value;
  StringExpr(const std::string &v) : value(v) {}
};

struct IdentExpr : Expr {
  std::string name;
  IdentExpr(const std::string &n) : name(n) {}
};

struct UnaryExpr : Expr {
  std::unique_ptr<Expr> operand;
  UnaryExpr(std::unique_ptr<Expr> o) : operand(std::move(o)) {}
};

struct BinaryExpr : Expr {
  std::unique_ptr<Expr> left;
  BinOp op;
  std::unique_ptr<Expr> right;
  BinaryExpr(std::unique_ptr<Expr> l, BinOp o, std::unique_ptr<Expr> r)
    : left(std::move(l)), op(o), right(std::move(r)) {}
};

struct CallExpr : Expr {
  std::string callee;
  std::vector<std::unique_ptr<Expr>> args;
};

struct Decl {
  virtual ~Decl() = default;
};

struct LetDecl : Decl {
  TypeAnnotation type_ann;
  std::string name;
  std::unique_ptr<Expr> init_expr;
};

struct Param {
  std::string name;
  TypeAnnotation type_ann;
};

struct Stmt {
  virtual ~Stmt() = default;
};

struct ExprStmt : Stmt {
  std::unique_ptr<Expr> expr;
  ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {}
};

struct LetStmt : Stmt {
  TypeAnnotation type_ann;
  std::string name;
  std::unique_ptr<Expr> init_expr;
};

struct ReturnStmt : Stmt {
  std::unique_ptr<Expr> value;
};

struct FnDecl : Decl {
  std::string name;
  std::vector<Param> params;
  TypeAnnotation return_type;
  std::vector<std::unique_ptr<Stmt>> body;
};
