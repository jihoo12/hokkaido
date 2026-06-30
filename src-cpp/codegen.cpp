#include "codegen.h"

#include <fstream>
#include <iostream>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include "cubical.h"

using namespace llvm;

// -------------------------------------------------------------------------
// LLVM type mapping
// -------------------------------------------------------------------------

Type *CodeGen::get_llvm_type(TypeKind kind) {
  switch (kind) {
    case TypeKind::Void:    return Type::getVoidTy(Context);
    case TypeKind::Int:     return Type::getInt64Ty(Context);
    case TypeKind::Float:   return Type::getDoubleTy(Context);
    case TypeKind::String:  return PointerType::getUnqual(Context);
    case TypeKind::Cubical: return Type::getInt64Ty(Context);
  }
  return nullptr;
}

Type *CodeGen::get_llvm_type(const TypeAnnotation &ann) {
  Type *base = get_llvm_type(ann.kind);
  for (int i = 0; i < ann.pointer_depth; i++)
    base = PointerType::getUnqual(Context);
  return base;
}

// -------------------------------------------------------------------------
// Top-level generate
// -------------------------------------------------------------------------

bool CodeGen::generate(const std::vector<std::unique_ptr<Decl>> &decls) {
  // Pass 1: create all function declarations so they can be referenced
  std::vector<FnDecl *> fn_decls;
  FnDecl *user_main = nullptr;
  for (auto &decl : decls) {
    if (auto *fn = dynamic_cast<FnDecl *>(decl.get())) {
      std::vector<Type *> param_types;
      for (auto &p : fn->params)
        param_types.push_back(get_llvm_type(p.type_ann));

      FunctionType *FT = FunctionType::get(
          get_llvm_type(fn->return_type), param_types, false);
      // Rename user "main" so it doesn't conflict with the auto-generated entry
      std::string llvm_name = fn->name;
      if (fn->name == "main") {
        llvm_name = "__user_main";
        user_main = fn;
      }
      Function::Create(FT, Function::ExternalLinkage, llvm_name, &M);
      fn_decls.push_back(fn);
    }
  }

  if (!user_main) {
    errs() << "Error: no main function defined (add 'fn main() -> int { ... }')\n";
    return false;
  }

  if (user_main->return_type.kind != TypeKind::Int) {
    errs() << "Error: main function must return int\n";
    return false;
  }

  // Pass 2: generate function bodies
  for (auto *fn : fn_decls) {
    std::string llvm_name = (fn->name == "main") ? "__user_main" : fn->name;
    if (!gen_fn_body(fn, M.getFunction(llvm_name)))
      return false;
  }

  // Pass 3: main entry point — wraps user main and handles top-level lets
  if (!gen_main_body(decls))
    return false;

  if (verifyModule(M, &errs())) {
    errs() << "Error: module verification failed\n";
    return false;
  }
  return true;
}

bool CodeGen::gen_main_body(const std::vector<std::unique_ptr<Decl>> &decls) {
  FunctionType *FT = FunctionType::get(Type::getInt32Ty(Context), false);
  MainFn = Function::Create(FT, Function::ExternalLinkage, "main", &M);
  EntryBB = BasicBlock::Create(Context, "entry", MainFn);
  Builder.SetInsertPoint(EntryBB);
  named_values.clear();
  named_types.clear();

  for (auto &decl : decls) {
    if (auto *let = dynamic_cast<LetDecl *>(decl.get())) {
      if (!gen_let_decl(let)) return false;
    }
  }

  // Call user main and return its value truncated to i32
  Function *user_main = M.getFunction("__user_main");
  if (user_main) {
    Value *result = Builder.CreateCall(user_main, {});
    result = Builder.CreateTrunc(result, Type::getInt32Ty(Context));
    Builder.CreateRet(result);
  } else {
    Builder.CreateRet(ConstantInt::get(Type::getInt32Ty(Context), 0));
  }
  return true;
}

// -------------------------------------------------------------------------
// Expression evaluation
// -------------------------------------------------------------------------

Value *CodeGen::eval_expr(Expr *expr, Type *expected_type) {
  if (auto *num = dynamic_cast<NumberExpr *>(expr)) {
    if (expected_type->isDoubleTy())
      return ConstantFP::get(expected_type, num->value);
    return ConstantInt::get(expected_type, (int64_t)num->value);
  }

  if (auto *str = dynamic_cast<StringExpr *>(expr)) {
    return Builder.CreateGlobalString(str->value, "str");
  }

  if (auto *id = dynamic_cast<IdentExpr *>(expr)) {
    auto it = named_values.find(id->name);
    if (it == named_values.end()) {
      errs() << "Error: undefined variable '" << id->name << "'\n";
      return nullptr;
    }
    return Builder.CreateLoad(expected_type, it->second, id->name);
  }

  if (auto *null_expr = dynamic_cast<NullExpr *>(expr)) {
    return ConstantPointerNull::get(cast<PointerType>(expected_type));
  }

  if (auto *addr = dynamic_cast<AddressOfExpr *>(expr)) {
    if (auto *id = dynamic_cast<IdentExpr *>(addr->operand.get())) {
      auto it = named_values.find(id->name);
      if (it == named_values.end()) {
        errs() << "Error: undefined variable '" << id->name << "'\n";
        return nullptr;
      }
      return it->second;
    }
    errs() << "Error: address-of requires a variable\n";
    return nullptr;
  }

  if (auto *deref = dynamic_cast<DerefExpr *>(expr)) {
    Value *ptr = nullptr;
    if (auto *id = dynamic_cast<IdentExpr *>(deref->operand.get())) {
      auto it = named_values.find(id->name);
      if (it == named_values.end()) {
        errs() << "Error: undefined variable '" << id->name << "'\n";
        return nullptr;
      }
      ptr = Builder.CreateLoad(it->second->getAllocatedType(), it->second, id->name);
    } else {
      ptr = eval_expr(deref->operand.get(), PointerType::getUnqual(Context));
    }
    if (!ptr) return nullptr;
    return Builder.CreateLoad(expected_type, ptr);
  }

  if (auto *un = dynamic_cast<UnaryExpr *>(expr)) {
    Value *op = eval_expr(un->operand.get(), expected_type);
    if (!op) return nullptr;
    if (expected_type->isDoubleTy())
      return Builder.CreateFNeg(op);
    return Builder.CreateNeg(op);
  }

  if (auto *bin = dynamic_cast<BinaryExpr *>(expr)) {
    Value *l = eval_expr(bin->left.get(), expected_type);
    Value *r = eval_expr(bin->right.get(), expected_type);
    if (!l || !r) return nullptr;

    bool is_float = expected_type->isDoubleTy();
    switch (bin->op) {
      case BinOp::Add: return is_float ? Builder.CreateFAdd(l, r) : Builder.CreateAdd(l, r);
      case BinOp::Sub: return is_float ? Builder.CreateFSub(l, r) : Builder.CreateSub(l, r);
      case BinOp::Mul: return is_float ? Builder.CreateFMul(l, r) : Builder.CreateMul(l, r);
      case BinOp::Div: return is_float ? Builder.CreateFDiv(l, r) : Builder.CreateSDiv(l, r);
      case BinOp::Eq: {
        Value *cmp = Builder.CreateICmpEQ(l, r);
        return Builder.CreateZExt(cmp, Type::getInt64Ty(Context));
      }
      case BinOp::Ne: {
        Value *cmp = Builder.CreateICmpNE(l, r);
        return Builder.CreateZExt(cmp, Type::getInt64Ty(Context));
      }
      case BinOp::Less: {
        Value *cmp = Builder.CreateICmpSLT(l, r);
        return Builder.CreateZExt(cmp, Type::getInt64Ty(Context));
      }
      case BinOp::Greater: {
        Value *cmp = Builder.CreateICmpSGT(l, r);
        return Builder.CreateZExt(cmp, Type::getInt64Ty(Context));
      }
      case BinOp::Le: {
        Value *cmp = Builder.CreateICmpSLE(l, r);
        return Builder.CreateZExt(cmp, Type::getInt64Ty(Context));
      }
      case BinOp::Ge: {
        Value *cmp = Builder.CreateICmpSGE(l, r);
        return Builder.CreateZExt(cmp, Type::getInt64Ty(Context));
      }
    }
  }

  if (auto *call = dynamic_cast<CallExpr *>(expr)) {
    Function *callee = M.getFunction(call->callee);
    if (!callee) {
      errs() << "Error: undefined function '" << call->callee << "'\n";
      return nullptr;
    }
    if (callee->arg_size() != call->args.size()) {
      errs() << "Error: wrong number of arguments to '" << call->callee << "'\n";
      return nullptr;
    }
    std::vector<Value *> args;
    for (size_t i = 0; i < call->args.size(); i++) {
      Type *param_type = callee->getArg(i)->getType();
      Value *arg = eval_expr(call->args[i].get(), param_type);
      if (!arg) return nullptr;
      args.push_back(arg);
    }
    return Builder.CreateCall(callee, args);
  }

  if (auto *asm_ = dynamic_cast<AsmExpr *>(expr)) {
    FunctionType *FT = FunctionType::get(Type::getVoidTy(Context), false);
    InlineAsm *IA = InlineAsm::get(FT, asm_->asm_code, "", true, false);
    Builder.CreateCall(IA);
    return ConstantInt::get(Type::getInt64Ty(Context), 0);
  }

  if (auto *assign = dynamic_cast<AssignExpr *>(expr)) {
    Value *val = eval_expr(assign->value.get(), expected_type);
    if (!val) return nullptr;

    if (auto *id = dynamic_cast<IdentExpr *>(assign->target.get())) {
      auto it = named_values.find(id->name);
      if (it == named_values.end()) {
        errs() << "Error: undefined variable '" << id->name << "'\n";
        return nullptr;
      }
      Builder.CreateStore(val, it->second);
    } else if (auto *deref = dynamic_cast<DerefExpr *>(assign->target.get())) {
      Value *ptr = nullptr;
      if (auto *id2 = dynamic_cast<IdentExpr *>(deref->operand.get())) {
        auto it = named_values.find(id2->name);
        if (it == named_values.end()) {
          errs() << "Error: undefined variable '" << id2->name << "'\n";
          return nullptr;
        }
        ptr = Builder.CreateLoad(it->second->getAllocatedType(), it->second, id2->name);
      } else {
        ptr = eval_expr(deref->operand.get(), PointerType::getUnqual(Context));
      }
      if (!ptr) return nullptr;
      Builder.CreateStore(val, ptr);
    }
    return val;
  }

  errs() << "Error: unknown expression type\n";
  return nullptr;
}

// -------------------------------------------------------------------------
// Per-type value generators
// -------------------------------------------------------------------------

Value *CodeGen::eval_int_init(Expr *expr) {
  if (auto *id = dynamic_cast<IdentExpr *>(expr)) {
    auto it = named_values.find(id->name);
    if (it == named_values.end()) {
      errs() << "Error: undefined variable '" << id->name << "'\n";
      return nullptr;
    }
    return Builder.CreateLoad(Type::getInt64Ty(Context), it->second, id->name);
  }
  return eval_expr(expr, Type::getInt64Ty(Context));
}

Value *CodeGen::eval_float_init(Expr *expr) {
  return eval_expr(expr, Type::getDoubleTy(Context));
}

Value *CodeGen::eval_string_init(Expr *expr) {
  return eval_expr(expr, PointerType::getUnqual(Context));
}

Value *CodeGen::eval_cubical_init(Expr *expr, std::string *debug_out) {
  auto *str = dynamic_cast<StringExpr *>(expr);
  if (!str) {
    errs() << "Error: cubical variable requires a string (inline source or file path)\n";
    return nullptr;
  }

  std::string cubical_source = str->value;

  if (cubical_source.size() >= 4 &&
      (cubical_source.substr(cubical_source.size() - 4) == ".cub" ||
       cubical_source.substr(cubical_source.size() - 5) == ".uwuc")) {
    std::ifstream ifs(cubical_source);
    if (!ifs) {
      errs() << "Error: cannot open cubical file '" << cubical_source << "'\n";
      return nullptr;
    }
    cubical_source.assign((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
  }

  cubical_value cv(cubical_source);
  if (!cv.valid()) {
    errs() << "Error: cubical evaluation failed\n";
    return nullptr;
  }

  int64_t int_val = cv.as_int();
  if (int_val >= 0) {
    if (debug_out)
      *debug_out = std::to_string(int_val) + "  (from cubical: " + cv.str() + ")";
    return ConstantInt::get(Type::getInt64Ty(Context), int_val);
  }

  if (debug_out)
    *debug_out = "\"" + cv.str() + "\"";
  return Builder.CreateGlobalString(cv.str(), "cubical_result");
}

// -------------------------------------------------------------------------
// Alloca + store helper
// -------------------------------------------------------------------------

bool CodeGen::alloc_and_store(const std::string &name, TypeKind kind,
                               Value *init, Type *llvm_type) {
  AllocaInst *alloca = Builder.CreateAlloca(llvm_type, nullptr, name);
  Builder.CreateStore(init, alloca);
  named_values[name] = alloca;
  named_types[name] = kind;
  return true;
}

// -------------------------------------------------------------------------
// Let declarations (top-level)
// -------------------------------------------------------------------------

bool CodeGen::gen_let_decl(LetDecl *decl) {
  Type *llvm_type = get_llvm_type(decl->type_ann);
  Value *init = nullptr;
  std::string debug;

  if (decl->type_ann.pointer_depth > 0) {
    init = eval_expr(decl->init_expr.get(), llvm_type);
    if (!init) return false;
    return alloc_and_store(decl->name, decl->type_ann.kind, init, llvm_type);
  }

  switch (decl->type_ann.kind) {
    case TypeKind::Void:
      errs() << "Error: variable cannot have void type\n";
      return false;
    case TypeKind::Int:
      init = eval_int_init(decl->init_expr.get());
      break;
    case TypeKind::Float:
      init = eval_float_init(decl->init_expr.get());
      break;
    case TypeKind::String:
      init = eval_string_init(decl->init_expr.get());
      break;
    case TypeKind::Cubical:
      init = eval_cubical_init(decl->init_expr.get(), &debug);
      if (init) std::cout << "  " << decl->name << " = " << debug << "\n";
      break;
  }
  if (!init) return false;
  return alloc_and_store(decl->name, decl->type_ann.kind, init, llvm_type);
}

bool CodeGen::gen_let_stmt(LetStmt *stmt) {
  Type *llvm_type = get_llvm_type(stmt->type_ann);
  Value *init = nullptr;

  if (stmt->type_ann.pointer_depth > 0) {
    init = eval_expr(stmt->init_expr.get(), llvm_type);
    if (!init) return false;
    return alloc_and_store(stmt->name, stmt->type_ann.kind, init, llvm_type);
  }

  switch (stmt->type_ann.kind) {
    case TypeKind::Void:
      errs() << "Error: variable cannot have void type\n";
      return false;
    case TypeKind::Int:
      init = eval_int_init(stmt->init_expr.get());
      break;
    case TypeKind::Float:
      init = eval_float_init(stmt->init_expr.get());
      break;
    case TypeKind::String:
      init = eval_string_init(stmt->init_expr.get());
      break;
    case TypeKind::Cubical:
      errs() << "Error: cubical type not supported in let statements\n";
      return false;
  }
  if (!init) return false;
  return alloc_and_store(stmt->name, stmt->type_ann.kind, init, llvm_type);
}

// -------------------------------------------------------------------------
// Functions
// -------------------------------------------------------------------------

bool CodeGen::gen_fn_body(FnDecl *decl, Function *fn) {
  BasicBlock *BB = BasicBlock::Create(Context, "entry", fn);
  Builder.SetInsertPoint(BB);

  // Save outer scope
  auto saved_values = std::move(named_values);
  auto saved_types = std::move(named_types);
  named_values.clear();
  named_types.clear();

  // Allocate and store parameters
  size_t i = 0;
  for (auto &arg : fn->args()) {
    arg.setName(decl->params[i].name);
    AllocaInst *alloca = Builder.CreateAlloca(arg.getType(), nullptr, arg.getName());
    Builder.CreateStore(&arg, alloca);
    named_values[std::string(arg.getName())] = alloca;
    named_types[std::string(arg.getName())] = decl->params[i].type_ann.kind;
    i++;
  }

  // Generate body
  for (auto &stmt : decl->body) {
    if (!gen_stmt(stmt.get())) {
      named_values = std::move(saved_values);
      named_types = std::move(saved_types);
      return false;
    }
  }

  // If function doesn't return, add a default return
  Type *ret_type = fn->getReturnType();
  if (!Builder.GetInsertBlock()->getTerminator()) {
    if (ret_type->isVoidTy())
      Builder.CreateRetVoid();
    else if (ret_type->isIntegerTy(64))
      Builder.CreateRet(ConstantInt::get(ret_type, 0));
    else if (ret_type->isDoubleTy())
      Builder.CreateRet(ConstantFP::get(ret_type, 0.0));
    else
      Builder.CreateRet(ConstantPointerNull::get(cast<PointerType>(ret_type)));
  }

  // Restore scope
  named_values = std::move(saved_values);
  named_types = std::move(saved_types);
  return true;
}

// -------------------------------------------------------------------------
// Statements
// -------------------------------------------------------------------------

bool CodeGen::gen_stmt(Stmt *stmt) {
  if (auto *let = dynamic_cast<LetStmt *>(stmt))
    return gen_let_stmt(let);
  if (auto *ret = dynamic_cast<ReturnStmt *>(stmt))
    return gen_return_stmt(ret);
  if (auto *if_ = dynamic_cast<IfStmt *>(stmt))
    return gen_if_stmt(if_);
  if (auto *for_ = dynamic_cast<ForStmt *>(stmt))
    return gen_for_stmt(for_);
  if (auto *expr = dynamic_cast<ExprStmt *>(stmt)) {
    Value *v = eval_expr(expr->expr.get(), Type::getInt64Ty(Context));
    return v != nullptr;
  }
  errs() << "Error: unknown statement type\n";
  return false;
}

bool CodeGen::gen_return_stmt(ReturnStmt *stmt) {
  Function *fn = Builder.GetInsertBlock()->getParent();
  Type *ret_type = fn->getReturnType();

  if (ret_type->isVoidTy()) {
    Builder.CreateRetVoid();
    return true;
  }

  if (!stmt->value) {
    errs() << "Error: non-void function must return a value\n";
    return false;
  }

  Value *val = eval_expr(stmt->value.get(), ret_type);
  if (!val) return false;
  Builder.CreateRet(val);
  return true;
}

bool CodeGen::gen_if_stmt(IfStmt *stmt) {
  Function *fn = Builder.GetInsertBlock()->getParent();

  Value *cond = eval_expr(stmt->condition.get(), Type::getInt64Ty(Context));
  if (!cond) return false;
  cond = Builder.CreateICmpNE(cond, ConstantInt::get(Type::getInt64Ty(Context), 0));

  BasicBlock *then_bb = BasicBlock::Create(Context, "if.then", fn);
  BasicBlock *else_bb = BasicBlock::Create(Context, "if.else", fn);
  BasicBlock *merge_bb = BasicBlock::Create(Context, "if.end", fn);

  Builder.CreateCondBr(cond, then_bb, else_bb);

  Builder.SetInsertPoint(then_bb);
  for (auto &s : stmt->then_branch) {
    if (!gen_stmt(s.get())) return false;
  }
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(merge_bb);

  Builder.SetInsertPoint(else_bb);
  for (auto &s : stmt->else_branch) {
    if (!gen_stmt(s.get())) return false;
  }
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(merge_bb);

  Builder.SetInsertPoint(merge_bb);
  return true;
}

bool CodeGen::gen_for_stmt(ForStmt *stmt) {
  Function *fn = Builder.GetInsertBlock()->getParent();

  if (stmt->init) {
    if (!gen_stmt(stmt->init.get())) return false;
  }

  BasicBlock *cond_bb = BasicBlock::Create(Context, "for.cond", fn);
  BasicBlock *body_bb = BasicBlock::Create(Context, "for.body", fn);
  BasicBlock *update_bb = BasicBlock::Create(Context, "for.update", fn);
  BasicBlock *end_bb = BasicBlock::Create(Context, "for.end", fn);

  Builder.CreateBr(cond_bb);

  Builder.SetInsertPoint(cond_bb);
  if (stmt->condition) {
    Value *cond = eval_expr(stmt->condition.get(), Type::getInt64Ty(Context));
    if (!cond) return false;
    cond = Builder.CreateICmpNE(cond, ConstantInt::get(Type::getInt64Ty(Context), 0));
    Builder.CreateCondBr(cond, body_bb, end_bb);
  } else {
    Builder.CreateBr(body_bb);
  }

  Builder.SetInsertPoint(body_bb);
  for (auto &s : stmt->body) {
    if (!gen_stmt(s.get())) return false;
  }
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(update_bb);

  Builder.SetInsertPoint(update_bb);
  if (stmt->update) {
    Value *v = eval_expr(stmt->update.get(), Type::getInt64Ty(Context));
    if (!v) return false;
  }
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(cond_bb);

  Builder.SetInsertPoint(end_bb);
  return true;
}
