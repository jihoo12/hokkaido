#pragma once

#include <memory>
#include <string>

// =========================================================================
// Hokkaido Language — AST
// =========================================================================

enum TypeKind {
  TYPE_INT,
  TYPE_FLOAT,
  TYPE_STRING,
  TYPE_CUBICAL,
};

struct TypeAnnotation {
  TypeKind kind;
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

struct Decl {
  virtual ~Decl() = default;
};

struct LetDecl : Decl {
  TypeAnnotation type_ann;
  std::string name;
  std::unique_ptr<Expr> init_expr;
};

struct PrintDecl : Decl {
  std::string name;
};