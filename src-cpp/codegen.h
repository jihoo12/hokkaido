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
  llvm::Function *MainFn;
  llvm::BasicBlock *EntryBB;

  std::map<std::string, llvm::AllocaInst *> named_values;
  std::map<std::string, TypeKind> named_types;

public:
  CodeGen(llvm::LLVMContext &Ctx, llvm::Module &Mod, llvm::IRBuilder<> &Bld)
      : Context(Ctx), M(Mod), Builder(Bld), MainFn(nullptr), EntryBB(nullptr) {}

  bool generate(const std::vector<std::unique_ptr<Decl>> &decls);

private:
  bool gen_let_decl(LetDecl *decl);
  void gen_print_decl(PrintDecl *decl);
};