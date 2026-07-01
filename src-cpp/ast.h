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
  Enum,
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
  And, Or, Shr, Shl,
  BitAnd, BitOr, Xor
};

enum class UnaryOp {
  Neg, BitNot
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
  UnaryOp op;
  std::unique_ptr<Expr> operand;
  UnaryExpr(UnaryOp o, std::unique_ptr<Expr> e) : op(o), operand(std::move(e)) {}
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

struct CompoundAssignExpr : Expr {
  std::unique_ptr<Expr> target;
  BinOp op;
  std::unique_ptr<Expr> value;
  CompoundAssignExpr(std::unique_ptr<Expr> t, BinOp o, std::unique_ptr<Expr> v)
    : target(std::move(t)), op(o), value(std::move(v)) {}
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

// Pattern matching

struct Pattern {
  virtual ~Pattern() = default;
};

struct WildcardPattern : Pattern {};

struct LiteralPattern : Pattern {
  std::unique_ptr<Expr> value;
  LiteralPattern(std::unique_ptr<Expr> v) : value(std::move(v)) {}
};

struct VariablePattern : Pattern {
  std::string name;
  VariablePattern(const std::string &n) : name(n) {}
};

struct StructPattern : Pattern {
  std::string struct_name;
  // Each field: (field_name, sub_pattern)
  std::vector<std::pair<std::string, std::unique_ptr<Pattern>>> fields;
};

struct VariantPattern : Pattern {
  std::string enum_name;
  std::string variant_name;
  std::vector<std::pair<std::string, std::unique_ptr<Pattern>>> fields;
};

struct MatchArm {
  std::unique_ptr<Pattern> pattern;
  std::unique_ptr<Expr> expr;
};

struct MatchExpr : Expr {
  std::unique_ptr<Expr> value;
  std::vector<MatchArm> arms;
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

// Algebraic data types (tagged unions / Rust-style enums)

struct AdtVariant {
  std::string name;
  std::vector<StructField> fields;
};

struct AdtDecl : Decl {
  std::string name;
  std::vector<AdtVariant> variants;
};

struct ConstructorExpr : Expr {
  std::string enum_name;
  std::string variant_name;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
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
  // `extern fn foo(...) -> T` declarations: no body, declares a foreign
  // (typically C) symbol to link against rather than generating code for it.
  bool is_extern = false;
  // Whether the parameter list ends in `...` (a C-style variadic function,
  // e.g. printf). Only meaningful when is_extern is true.
  bool is_variadic = false;
};