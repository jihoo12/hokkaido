#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;

// --- [Context & State] ---

struct VariableInfo {
  Value *Address;
  Type *VarType;
};

struct CodeGenContext {
  LLVMContext &Context;
  Module *TheModule;
  IRBuilder<> &Builder;
  const DataLayout &DL;

  std::map<std::string, VariableInfo> NamedValues;
  std::map<std::string, BasicBlock *> LabelBlocks;
  Function *MainFn = nullptr;
  AllocaInst *CmpVal = nullptr;
};

// --- [1. Token & Lexer] ---

enum Token {
  tok_eof = -1,
  tok_let = -2,
  tok_identifier = -3,
  tok_number = -4,
  tok_asm = -5,
  tok_string = -6,
  tok_return = -7,
  tok_label = -8,
  tok_br = -9,
  tok_cmp = -10,
};

class Lexer {
public:
  double NumVal;
  std::string IdentifierStr;
  std::string StringVal;
  int Line = 1;
  int Col = 0;

private:
  std::string FileContent;
  size_t CurrentPos;
  int LastChar;
};

// --- [2. AST] ---

class ExprAST {
public:
};

class NumberExprAST : public ExprAST {
  double Val;

public:
};

class VariableExprAST : public ExprAST {
  std::string Name;

public:
};

class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
};

class AsmExprAST : public ExprAST {
  std::string AsmString;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
};

// --- [3. Parser] ---

class Parser {
  std::map<char, int> BinopPrecedence;

public:
};

// --- [4. Compiler Class] ---

class Compiler {
  Lexer &L;
  Parser &P;
  CodeGenContext &Ctx;
  int CurTok;

public:
};

int main(int argc, char *argv[]) {
  std::filesystem::path filePath(argv[1]);
  std::ifstream ifs(filePath);
  std::string Content;

  std::string TargetTriple;
  std::string Error;
  const Target *TheTarget;

  std::string CPU;
  TargetOptions opt;
  auto RM = std::optional<Reloc::Model>();
  std::unique_ptr<class TargetMachine> TargetMachine;

  LLVMContext Context;
  std::unique_ptr<Module> M;

  IRBuilder<> Builder(Context);
  CodeGenContext Ctx{Context, nullptr, Builder, M->getDataLayout()};

  Lexer L;
  Parser P;
  Compiler Comp{L, P, Ctx};

  FunctionType *FT;
  Function *MainFn;
  BasicBlock *BB;

  return 0;
}