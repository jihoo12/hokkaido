#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "ast.h"

// =========================================================================
// Hokkaido Language — Code Generator
// =========================================================================

class CodeGen {
  llvm::LLVMContext &Context;
  llvm::Module &M;
  llvm::IRBuilder<> &Builder;
  llvm::Function *MainFn = nullptr;
  llvm::BasicBlock *EntryBB = nullptr;

  std::map<std::string, llvm::AllocaInst *> named_values;
  std::map<std::string, TypeKind> named_types;
  std::map<std::string, TypeAnnotation> named_type_anns;

  // Registered struct types (name -> LLVM struct type)
  std::map<std::string, llvm::StructType *> struct_types;
  // Struct field types (name -> vector of (field_name, annotation))
  std::map<std::string, std::vector<std::pair<std::string, TypeAnnotation>>> struct_fields;

  // Registered enum types (name -> LLVM struct type)
  std::map<std::string, llvm::StructType *> enum_types;
  // Enum variant info: enum name -> vector of (variant_name, fields_vector)
  std::map<std::string, std::vector<std::pair<std::string, std::vector<StructField>>>> enum_variants;

public:
  CodeGen(llvm::LLVMContext &Ctx, llvm::Module &Mod, llvm::IRBuilder<> &Bld,
          bool Freestanding = false)
      : Context(Ctx), M(Mod), Builder(Bld), freestanding(Freestanding) {}

  bool generate(const std::vector<std::unique_ptr<Decl>> &decls);

private:
  // When true, `main` is generated as a raw ELF entry point (no CRT/libc):
  // instead of returning normally, it terminates via a direct `exit`
  // syscall, and `extern fn` declarations are rejected at compile time
  // since there is no libc to resolve them against.
  bool freestanding;

  // Top-level codegen
  bool gen_main_body(const std::vector<std::unique_ptr<Decl>> &decls);

  // Struct declarations
  void register_struct_decl(StructDecl *decl);
  llvm::StructType *get_struct_type(const std::string &name);
  int get_struct_field_index(const std::string &struct_name, const std::string &field_name);
  TypeAnnotation get_struct_field_type(const std::string &struct_name, const std::string &field_name);

  // Enum declarations
  void register_enum_decl(AdtDecl *decl);
  int get_enum_variant_index(const std::string &enum_name, const std::string &variant_name);
  const std::vector<StructField> *get_enum_variant_fields(
      const std::string &enum_name, const std::string &variant_name);

  // Resolve the type annotation for an expression without evaluating it
  TypeAnnotation resolve_expr_type(Expr *expr);

  // Get a pointer to the memory location of an lvalue expression
  llvm::Value *get_lvalue_ptr(Expr *expr, llvm::Type **out_type = nullptr);

  // Let declarations / statements
  bool gen_let_decl(LetDecl *decl);
  bool gen_let_stmt(LetStmt *stmt);
  bool alloc_and_store(const std::string &name, TypeKind kind,
                       llvm::Value *init, llvm::Type *llvm_type,
                       TypeAnnotation ann = {});
  bool alloc_and_store_array(const std::string &name, TypeKind kind,
                             int array_size, llvm::ArrayType *array_type,
                             llvm::Value *init,
                             TypeAnnotation ann = {});

  // Functions
  bool gen_fn_decl(FnDecl *decl);
  bool gen_fn_body(FnDecl *decl, llvm::Function *fn);

  // Statements
  bool gen_stmt(Stmt *stmt);
  bool gen_return_stmt(ReturnStmt *stmt);
  bool gen_if_stmt(IfStmt *stmt);
  bool gen_for_stmt(ForStmt *stmt);

  // Expression evaluation
  llvm::Value *eval_expr(Expr *expr, llvm::Type *expected_type);

  // Value generators (per-type)
  llvm::Value *eval_int_init(Expr *expr);
  llvm::Value *eval_float_init(Expr *expr);
  llvm::Value *eval_string_init(Expr *expr);
  llvm::Value *eval_cubical_init(Expr *expr, std::string *debug_out);

  // Array helpers
  llvm::Value *eval_array_init(Expr *expr, llvm::ArrayType *array_type);
  llvm::Value *eval_array_literal(ArrayLitExpr *arr, llvm::ArrayType *array_type);

  // LLVM type helpers
  llvm::Type *get_llvm_type(TypeKind kind);
  llvm::Type *get_llvm_type(const TypeAnnotation &ann);

  // Pattern matching helpers
  llvm::Value *gen_pattern_check(Pattern *pat, llvm::Value *val,
                                  const TypeAnnotation &val_ann);
  bool gen_pattern_bind(Pattern *pat, llvm::Value *val,
                         const TypeAnnotation &val_ann);
};