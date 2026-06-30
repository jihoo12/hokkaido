#pragma once

#include <memory>
#include <string>
#include <vector>

// =========================================================================
// Hokkaido Language — AST
// =========================================================================

enum class TypeKind {
  Void,
  Int8,
  Int32,
  Int64,
  Float16,
  Float32,
  Float64,
  Bool,
  String,
  Cubical,
  Struct,
};

struct TypeAnnotation {
  TypeKind kind;
  int pointer_depth = 0;
  int array_size = 0; // 0 means not an array
  std::string struct_name; // for Struct kind
};

enum class BinOp {
  Add, Sub, Mul, Div,
  Eq, Ne, Less, Greater, Le, Ge,
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

struct AsmExpr : Expr {
  std::string asm_code;
};

struct AssignExpr : Expr {
  std::unique_ptr<Expr> target;
  std::unique_ptr<Expr> value;
  AssignExpr(std::unique_ptr<Expr> t, std::unique_ptr<Expr> v)
    : target(std::move(t)), value(std::move(v)) {}
};

struct NullExpr : Expr {};

struct AddressOfExpr : Expr {
  std::unique_ptr<Expr> operand;
  AddressOfExpr(std::unique_ptr<Expr> o) : operand(std::move(o)) {}
};

struct DerefExpr : Expr {
  std::unique_ptr<Expr> operand;
  DerefExpr(std::unique_ptr<Expr> o) : operand(std::move(o)) {}
};

struct SubscriptExpr : Expr {
  std::unique_ptr<Expr> array;
  std::unique_ptr<Expr> index;
  SubscriptExpr(std::unique_ptr<Expr> a, std::unique_ptr<Expr> i)
    : array(std::move(a)), index(std::move(i)) {}
};

struct ArrayLitExpr : Expr {
  std::vector<std::unique_ptr<Expr>> elements;
};

struct FieldAccessExpr : Expr {
  std::unique_ptr<Expr> object;
  std::string field;
  FieldAccessExpr(std::unique_ptr<Expr> o, const std::string &f)
    : object(std::move(o)), field(f) {}
};

struct Decl {
  virtual ~Decl() = default;
};

struct LetDecl : Decl {
  TypeAnnotation type_ann;
  std::string name;
  std::unique_ptr<Expr> init_expr;
};

struct StructField {
  std::string name;
  TypeAnnotation type_ann;
};

struct StructDecl : Decl {
  std::string name;
  std::vector<StructField> fields;
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

struct IfStmt : Stmt {
  std::unique_ptr<Expr> condition;
  std::vector<std::unique_ptr<Stmt>> then_branch;
  std::vector<std::unique_ptr<Stmt>> else_branch;
};

struct ForStmt : Stmt {
  std::unique_ptr<Stmt> init;
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> update;
  std::vector<std::unique_ptr<Stmt>> body;
};

struct FnDecl : Decl {
  std::string name;
  std::vector<Param> params;
  TypeAnnotation return_type;
  std::vector<std::unique_ptr<Stmt>> body;
};