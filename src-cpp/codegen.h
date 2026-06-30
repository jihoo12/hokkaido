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

public:
  CodeGen(llvm::LLVMContext &Ctx, llvm::Module &Mod, llvm::IRBuilder<> &Bld)
      : Context(Ctx), M(Mod), Builder(Bld) {}

  bool generate(const std::vector<std::unique_ptr<Decl>> &decls);

private:
  // Top-level codegen
  bool gen_main_body(const std::vector<std::unique_ptr<Decl>> &decls);

  // Let declarations / statements
  bool gen_let_decl(LetDecl *decl);
  bool gen_let_stmt(LetStmt *stmt);
  bool alloc_and_store(const std::string &name, TypeKind kind,
                       llvm::Value *init, llvm::Type *llvm_type);

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

  // LLVM type helpers
  llvm::Type *get_llvm_type(TypeKind kind);
  llvm::Type *get_llvm_type(const TypeAnnotation &ann);

};