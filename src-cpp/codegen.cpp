#include "codegen.h"

#include <fstream>
#include <iostream>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include "cubical.h"

using namespace llvm;

bool CodeGen::generate(const std::vector<std::unique_ptr<Decl>> &decls) {
  // Create main function
  FunctionType *FT = FunctionType::get(Type::getInt32Ty(Context), false);
  MainFn = Function::Create(FT, Function::ExternalLinkage, "main", &M);
  EntryBB = BasicBlock::Create(Context, "entry", MainFn);
  Builder.SetInsertPoint(EntryBB);

  for (auto &decl : decls) {
    if (auto *let = dynamic_cast<LetDecl *>(decl.get())) {
      if (!gen_let_decl(let)) return false;
    } else if (auto *print = dynamic_cast<PrintDecl *>(decl.get())) {
      gen_print_decl(print);
    }
  }

  // Return 0
  Builder.CreateRet(ConstantInt::get(Type::getInt32Ty(Context), 0));

  // Verify
  if (verifyModule(M, &errs())) {
    errs() << "Error: module verification failed\n";
    return false;
  }

  return true;
}

bool CodeGen::gen_let_decl(LetDecl *decl) {
  Type *llvm_type = nullptr;
  Constant *init_val = nullptr;

  switch (decl->type_ann.kind) {
    case TYPE_INT: {
      llvm_type = Type::getInt64Ty(Context);
      double val = 0;
      if (auto *num = dynamic_cast<NumberExpr *>(decl->init_expr.get())) {
        val = num->value;
      } else if (auto *id = dynamic_cast<IdentExpr *>(decl->init_expr.get())) {
        // Reference to another variable
        auto it = named_values.find(id->name);
        if (it == named_values.end()) {
          errs() << "Error: undefined variable '" << id->name << "'\n";
          return false;
        }
        // Load from the existing alloca
        Value *loaded = Builder.CreateLoad(Type::getInt64Ty(Context), it->second, id->name);
        AllocaInst *alloca = Builder.CreateAlloca(Type::getInt64Ty(Context), nullptr, decl->name);
        Builder.CreateStore(loaded, alloca);
        named_values[decl->name] = alloca;
        named_types[decl->name] = TYPE_INT;
        return true;
      } else {
        errs() << "Error: int variable requires a number\n";
        return false;
      }
      init_val = ConstantInt::get(Type::getInt64Ty(Context), (int64_t)val);
      break;
    }
    case TYPE_FLOAT: {
      llvm_type = Type::getDoubleTy(Context);
      double val = 0;
      if (auto *num = dynamic_cast<NumberExpr *>(decl->init_expr.get())) {
        val = num->value;
      } else {
        errs() << "Error: float variable requires a number\n";
        return false;
      }
      init_val = ConstantFP::get(Type::getDoubleTy(Context), val);
      break;
    }
    case TYPE_STRING: {
      llvm_type = PointerType::getUnqual(Context);
      if (auto *str = dynamic_cast<StringExpr *>(decl->init_expr.get())) {
        init_val = Builder.CreateGlobalString(str->value, decl->name);
      } else {
        errs() << "Error: string variable requires a string literal\n";
        return false;
      }
      break;
    }
    case TYPE_CUBICAL: {
      // Cubical type: evaluate the cubical expression at compile time
      std::string cubical_source;

      if (auto *str = dynamic_cast<StringExpr *>(decl->init_expr.get())) {
        cubical_source = str->value;

        // Check if the string is a file path (ends with .cub or .uwuc)
        if (cubical_source.size() >= 4 &&
            (cubical_source.substr(cubical_source.size() - 4) == ".cub" ||
             cubical_source.substr(cubical_source.size() - 5) == ".uwuc")) {
          // It's a file path — read the file
          std::ifstream ifs(cubical_source);
          if (!ifs) {
            errs() << "Error: cannot open cubical file '" << cubical_source << "'\n";
            return false;
          }
          cubical_source.assign((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());
        }
        // Otherwise it's inline cubical source
      } else {
        errs() << "Error: cubical variable requires a string (inline source or file path)\n";
        return false;
      }

      // Evaluate the cubical expression
      cubical_value cv(cubical_source);
      if (!cv.valid()) {
        errs() << "Error: cubical evaluation failed for '" << decl->name << "'\n";
        return false;
      }

      // Try to extract an integer value (for Nat results)
      int64_t int_val = cv.as_int();
      if (int_val >= 0) {
        // Store as i64
        llvm_type = Type::getInt64Ty(Context);
        init_val = ConstantInt::get(Type::getInt64Ty(Context), int_val);
        std::cout << "  " << decl->name << " = " << int_val
                  << "  (from cubical: " << cv.str() << ")\n";
      } else {
        // Store the result string as a global string pointer
        llvm_type = PointerType::getUnqual(Context);
        init_val = Builder.CreateGlobalString(cv.str(), decl->name);
        std::cout << "  " << decl->name << " = \"" << cv.str() << "\"\n";
      }
      break;
    }
  }

  // Create alloca and store initial value
  AllocaInst *alloca = Builder.CreateAlloca(llvm_type, nullptr, decl->name);
  if (init_val) {
    Builder.CreateStore(init_val, alloca);
  }
  named_values[decl->name] = alloca;
  named_types[decl->name] = decl->type_ann.kind;

  return true;
}

void CodeGen::gen_print_decl(PrintDecl *decl) {
  auto it = named_values.find(decl->name);
  if (it == named_values.end()) {
    errs() << "Warning: print of undefined variable '" << decl->name << "'\n";
    return;
  }

  auto type_it = named_types.find(decl->name);
  TypeKind kind = (type_it != named_types.end()) ? type_it->second : TYPE_INT;

  // For now, just generate a no-op (LLVM doesn't have a simple print intrinsic)
  // In a real compiler, you'd call printf or similar
  Value *loaded = Builder.CreateLoad(it->second->getAllocatedType(), it->second, decl->name);

  // Print the value at compile time (since we're a compiler)
  if (auto *ci = dyn_cast<ConstantInt>(loaded)) {
    std::cout << "  " << decl->name << " = " << ci->getSExtValue() << "\n";
  } else if (auto *cf = dyn_cast<ConstantFP>(loaded)) {
    std::cout << "  " << decl->name << " = " << cf->getValueAPF().convertToDouble() << "\n";
  } else if (auto *ce = dyn_cast<ConstantExpr>(loaded)) {
    std::cout << "  " << decl->name << " = (constant expression)\n";
  } else {
    std::cout << "  " << decl->name << " = (runtime value)\n";
  }
}