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

// --- [Utility] ---

// Helper to convert GCC-style asm syntax to LLVM style
std::string ConvertAsmString(const std::string &In) {
  std::string Out;
  for (size_t i = 0; i < In.size(); ++i) {
    if (In[i] == '%') {
      if (i + 1 < In.size() && isdigit(static_cast<unsigned char>(In[i + 1]))) {
        Out += '$'; // Convert operand placeholder
      } else {
        Out += '%'; // Keep register names like %eax
      }
    } else if (In[i] == '$') {
      Out += "$$"; // Escape immediate prefix for LLVM
    } else {
      Out += In[i];
    }
  }
  return Out;
}

void LogError(const char *Str, int Line = -1) {
  errs() << "Error: " << Str;
  if (Line != -1)
    errs() << " (Line: " << Line << ")";
  errs() << "\n";
}

Value *LogErrorV(const char *Str, int Line = -1) {
  LogError(Str, Line);
  return nullptr;
}

// Type Casting Helper
Value *createCast(IRBuilder<> &Builder, Value *Val, Type *DestTy) {
  Type *SrcTy = Val->getType();
  if (SrcTy == DestTy)
    return Val;

  if (SrcTy->isIntegerTy() && DestTy->isDoubleTy()) {
    return Builder.CreateSIToFP(Val, DestTy, "casttmp");
  }
  if (SrcTy->isDoubleTy() && DestTy->isIntegerTy()) {
    return Builder.CreateFPToSI(Val, DestTy, "casttmp");
  }

  // Fallback
  return Val;
}

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

  CodeGenContext(LLVMContext &C, Module *M, IRBuilder<> &B, const DataLayout &D)
      : Context(C), TheModule(M), Builder(B), DL(D) {}

  BasicBlock *GetOrCreateBlock(const std::string &Name) {
    if (LabelBlocks.find(Name) == LabelBlocks.end()) {
      LabelBlocks[Name] = BasicBlock::Create(Context, Name, MainFn);
    }
    return LabelBlocks[Name];
  }

  AllocaInst *CreateEntryBlockAlloca(StringRef VarName, Type *VarType) {
    IRBuilder<> TmpB(&MainFn->getEntryBlock(), MainFn->getEntryBlock().begin());
    return TmpB.CreateAlloca(VarType, nullptr, VarName);
  }
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

  Lexer(std::string content)
      : FileContent(std::move(content)), CurrentPos(0), LastChar(' ') {}

  int get_token() {
    while (isspace(static_cast<unsigned char>(LastChar))) {
      LastChar = next_char();
    }

    if (LastChar == '#' || (LastChar == '/' && HasNext('/'))) {
      do {
        LastChar = next_char();
      } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
      if (LastChar != EOF)
        return get_token();
    }

    if (isalpha(static_cast<unsigned char>(LastChar))) {
      IdentifierStr = LastChar;
      while (isalnum(static_cast<unsigned char>((LastChar = next_char()))) ||
             LastChar == '_')
        IdentifierStr += LastChar;

      if (IdentifierStr == "let")
        return tok_let;
      if (IdentifierStr == "asm")
        return tok_asm;
      if (IdentifierStr == "return")
        return tok_return;
      if (IdentifierStr == "label")
        return tok_label;
      if (IdentifierStr == "br")
        return tok_br;
      if (IdentifierStr == "cmp")
        return tok_cmp;
      return tok_identifier;
    }

    if (isdigit(static_cast<unsigned char>(LastChar)) || LastChar == '.') {
      std::string NumStr;
      do {
        NumStr += LastChar;
        LastChar = next_char();
      } while (isdigit(static_cast<unsigned char>(LastChar)) ||
               LastChar == '.');

      char *end;
      NumVal = strtod(NumStr.c_str(), &end);
      if (end == NumStr.c_str()) {
        LogError("Invalid number format");
        NumVal = 0.0;
      }
      return tok_number;
    }

    if (LastChar == '"') {
      StringVal = "";
      while ((LastChar = next_char()) != '"' && LastChar != EOF)
        StringVal += LastChar;
      LastChar = next_char();
      return tok_string;
    }

    if (LastChar == EOF)
      return tok_eof;

    int ThisChar = LastChar;
    LastChar = next_char();
    return ThisChar;
  }

  int getLine() const { return Line; }
  int getCol() const { return Col; }

private:
  std::string FileContent;
  size_t CurrentPos;
  int LastChar;

  bool HasNext(char Check) {
    if (CurrentPos >= FileContent.size())
      return false;
    return FileContent[CurrentPos] == Check;
  }

  int next_char() {
    if (CurrentPos >= FileContent.size())
      return EOF;
    char c = FileContent[CurrentPos++];
    if (c == '\n') {
      Line++;
      Col = 0;
    } else {
      Col++;
    }
    return c;
  }
};

// --- [2. AST] ---

class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen(CodeGenContext &Ctx) = 0;
  virtual Value *codegenAddress(CodeGenContext &Ctx) { return nullptr; }
};

class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}

  Value *codegen(CodeGenContext &Ctx) override {
    return ConstantFP::get(Ctx.Builder.getDoubleTy(), Val);
  }
};

class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(std::string Name) : Name(std::move(Name)) {}

  Value *codegen(CodeGenContext &Ctx) override {
    if (Ctx.NamedValues.find(Name) == Ctx.NamedValues.end())
      return LogErrorV(("Unknown variable name: " + Name).c_str());

    auto &info = Ctx.NamedValues[Name];
    auto *Load =
        Ctx.Builder.CreateLoad(info.VarType, info.Address, Name.c_str());
    Load->setAlignment(Ctx.DL.getPrefTypeAlign(info.VarType));
    return Load;
  }

  Value *codegenAddress(CodeGenContext &Ctx) override {
    if (Ctx.NamedValues.find(Name) == Ctx.NamedValues.end())
      return nullptr;
    return Ctx.NamedValues[Name].Address;
  }
};

class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

  Value *codegen(CodeGenContext &Ctx) override {
    Value *L = LHS->codegen(Ctx);
    Value *R = RHS->codegen(Ctx);
    if (!L || !R)
      return nullptr;

    Type *TyL = L->getType();
    Type *TyR = R->getType();

    if (TyL->isDoubleTy() || TyR->isDoubleTy()) {
      L = createCast(Ctx.Builder, L, Ctx.Builder.getDoubleTy());
      R = createCast(Ctx.Builder, R, Ctx.Builder.getDoubleTy());
    } else {
      L = createCast(Ctx.Builder, L, Ctx.Builder.getInt32Ty());
      R = createCast(Ctx.Builder, R, Ctx.Builder.getInt32Ty());
    }

    bool isDouble = L->getType()->isDoubleTy();

    switch (Op) {
    case '+':
      return isDouble ? Ctx.Builder.CreateFAdd(L, R, "addtmp")
                      : Ctx.Builder.CreateAdd(L, R, "addtmp");
    case '-':
      return isDouble ? Ctx.Builder.CreateFSub(L, R, "subtmp")
                      : Ctx.Builder.CreateSub(L, R, "subtmp");
    case '*':
      return isDouble ? Ctx.Builder.CreateFMul(L, R, "multmp")
                      : Ctx.Builder.CreateMul(L, R, "multmp");
    case '/':
      return isDouble ? Ctx.Builder.CreateFDiv(L, R, "divtmp")
                      : Ctx.Builder.CreateSDiv(L, R, "divtmp");
    default:
      return LogErrorV("Invalid binary operator");
    }
  }
};

class AsmExprAST : public ExprAST {
  std::string AsmString;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  AsmExprAST(std::string Str, std::vector<std::unique_ptr<ExprAST>> Args)
      : AsmString(std::move(Str)), Args(std::move(Args)) {}

  Value *codegen(CodeGenContext &Ctx) override {
    std::vector<Value *> ArgValues;
    std::vector<Type *> ArgTypes;
    std::string Constraints = "";

    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
      Value *ArgVal = nullptr;

      // Try to get address first (L-value)
      Value *Addr = Args[i]->codegenAddress(Ctx);
      if (Addr) {
        ArgVal = Addr;
      } else {
        // Fallback to R-value
        ArgVal = Args[i]->codegen(Ctx);
      }

      if (!ArgVal)
        return nullptr;

      if (i > 0)
        Constraints += ",";
      Constraints += "r";

      ArgValues.push_back(ArgVal);
      ArgTypes.push_back(ArgVal->getType());
    }

    FunctionType *FTy =
        FunctionType::get(Ctx.Builder.getVoidTy(), ArgTypes, false);
    InlineAsm *IA = InlineAsm::get(FTy, AsmString, Constraints, true);
    return Ctx.Builder.CreateCall(IA, ArgValues);
  }
};

// --- [3. Parser] ---

class Parser {
  std::map<char, int> BinopPrecedence;

public:
  Parser() {
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;
    BinopPrecedence['/'] = 40;
  }

  int GetTokPrecedence(int Tok) {
    if (Tok < 0 || !isascii(Tok))
      return -1;
    int Prec = BinopPrecedence[Tok];
    return Prec <= 0 ? -1 : Prec;
  }

  std::unique_ptr<ExprAST> ParsePrimary(Lexer &L, int &CurTok) {
    if (CurTok == tok_number) {
      auto Res = std::make_unique<NumberExprAST>(L.NumVal);
      CurTok = L.get_token();
      return Res;
    }
    if (CurTok == tok_identifier) {
      auto Res = std::make_unique<VariableExprAST>(L.IdentifierStr);
      CurTok = L.get_token();
      return Res;
    }
    if (CurTok == '(') {
      CurTok = L.get_token();
      auto V = ParseExpression(L, CurTok);
      if (!V)
        return nullptr;
      if (CurTok != ')') {
        LogError("expected ')'", L.getLine());
        return nullptr;
      }
      CurTok = L.get_token();
      return V;
    }
    LogError("Unknown token when expecting an expression", L.getLine());
    return nullptr;
  }

  std::unique_ptr<ExprAST> ParseBinOpRHS(Lexer &L, int ExprPrec,
                                         std::unique_ptr<ExprAST> LHS,
                                         int &CurTok) {
    while (true) {
      int TokPrec = GetTokPrecedence(CurTok);
      if (TokPrec < ExprPrec)
        return LHS;

      int BinOp = CurTok;
      CurTok = L.get_token();

      auto RHS = ParsePrimary(L, CurTok);
      if (!RHS)
        return nullptr;

      int NextPrec = GetTokPrecedence(CurTok);
      if (TokPrec < NextPrec) {
        RHS = ParseBinOpRHS(L, TokPrec + 1, std::move(RHS), CurTok);
        if (!RHS)
          return nullptr;
      }
      LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS),
                                            std::move(RHS));
    }
  }

  std::unique_ptr<ExprAST> ParseExpression(Lexer &L, int &CurTok) {
    auto LHS = ParsePrimary(L, CurTok);
    if (!LHS)
      return nullptr;
    return ParseBinOpRHS(L, 0, std::move(LHS), CurTok);
  }

  std::unique_ptr<ExprAST> ParseAsm(Lexer &L, int &CurTok) {
    CurTok = L.get_token();
    if (CurTok != tok_string) {
      LogError("Expected string after asm");
      return nullptr;
    }

    std::string AsmCode = ConvertAsmString(L.StringVal);
    CurTok = L.get_token();

    std::vector<std::unique_ptr<ExprAST>> Args;
    while (CurTok == ',') {
      CurTok = L.get_token(); // eat ','
      auto Arg = ParseExpression(L, CurTok);
      if (!Arg)
        return nullptr;
      Args.push_back(std::move(Arg));
    }

    return std::make_unique<AsmExprAST>(AsmCode, std::move(Args));
  }

  Type *getLLVMType(const std::string &Name, LLVMContext &Ctx) {
    if (Name == "i32")
      return Type::getInt32Ty(Ctx);
    if (Name == "i64")
      return Type::getInt64Ty(Ctx);
    if (Name == "double")
      return Type::getDoubleTy(Ctx);
    return nullptr;
  }
};

// --- [4. Compiler Class] ---

class Compiler {
  Lexer &L;
  Parser &P;
  CodeGenContext &Ctx;
  int CurTok;

public:
  Compiler(Lexer &Lex, Parser &Par, CodeGenContext &Context)
      : L(Lex), P(Par), Ctx(Context) {
    CurTok = L.get_token();
  }

  bool HandleLet() {
    CurTok = L.get_token(); // eat let
    std::string TypeName = L.IdentifierStr;
    Type *VarType = P.getLLVMType(TypeName, Ctx.Context);
    if (!VarType) {
      LogErrorV(("Unknown type '" + TypeName + "'").c_str(), L.getLine());
      return false;
    }

    CurTok = L.get_token();                // eat Type
    std::string VarName = L.IdentifierStr; // Name

    if (Ctx.NamedValues.find(VarName) != Ctx.NamedValues.end()) {
      LogError(("Variable redefinition: " + VarName).c_str(), L.getLine());
      return false;
    }

    CurTok = L.get_token(); // eat Name
    if (CurTok != '=') {
      LogErrorV("Expected '=' after variable name", L.getLine());
      return false;
    }
    CurTok = L.get_token(); // eat =

    auto Expr = P.ParseExpression(L, CurTok);
    if (Expr) {
      Value *Val = Expr->codegen(Ctx);
      if (!Val)
        return false;

      Val = createCast(Ctx.Builder, Val, VarType);

      AllocaInst *Alloca = Ctx.CreateEntryBlockAlloca(VarName, VarType);
      Align PreferredAlign = Ctx.DL.getPrefTypeAlign(VarType);
      Alloca->setAlignment(PreferredAlign);
      Ctx.Builder.CreateStore(Val, Alloca)->setAlignment(PreferredAlign);
      Ctx.NamedValues[VarName] = {Alloca, VarType};
    }
    return true;
  }

  bool HandleAsm() {
    auto AsmAST = P.ParseAsm(L, CurTok);
    if (AsmAST) {
      Value *V = AsmAST->codegen(Ctx);
      if (!V)
        return false;
      if (CurTok == ';')
        CurTok = L.get_token();
      return true;
    }
    return false;
  }

  bool HandleReturn(bool &HasReturn) {
    CurTok = L.get_token();
    auto Expr = P.ParseExpression(L, CurTok);
    if (Expr) {
      Value *retVal = Expr->codegen(Ctx);
      retVal = createCast(Ctx.Builder, retVal, Ctx.Builder.getInt32Ty());
      Ctx.Builder.CreateRet(retVal);
      HasReturn = true;
      return true;
    }
    return false;
  }

  bool HandleLabel() {
    CurTok = L.get_token(); // eat label
    if (CurTok != tok_identifier) {
      LogErrorV("Expected identifier after label", L.getLine());
      return false;
    }
    std::string Name = L.IdentifierStr;
    CurTok = L.get_token();

    BasicBlock *BB = Ctx.GetOrCreateBlock(Name);

    if (auto *CurBB = Ctx.Builder.GetInsertBlock()) {
      if (CurBB != BB && !CurBB->getTerminator()) {
        Ctx.Builder.CreateBr(BB);
      }
    }

    Ctx.Builder.SetInsertPoint(BB);
    return true;
  }

  bool HandleBr() {
    CurTok = L.get_token(); // eat br
    if (CurTok != tok_identifier) {
      LogErrorV("Expected identifier after br", L.getLine());
      return false;
    }
    std::string Name = L.IdentifierStr;
    CurTok = L.get_token();

    BasicBlock *TargetBB = Ctx.GetOrCreateBlock(Name);

    Value *Cmps =
        Ctx.Builder.CreateLoad(Ctx.Builder.getInt32Ty(), Ctx.CmpVal, "cmp_val");
    Value *Cond =
        Ctx.Builder.CreateICmpNE(Cmps, Ctx.Builder.getInt32(0), "br_cond");

    BasicBlock *FallThroughBB =
        BasicBlock::Create(Ctx.Context, "fallthrough", Ctx.MainFn);

    Ctx.Builder.CreateCondBr(Cond, TargetBB, FallThroughBB);
    Ctx.Builder.SetInsertPoint(FallThroughBB);
    return true;
  }

  bool HandleCmp() {
    CurTok = L.get_token(); // eat cmp
    auto LHS = P.ParseExpression(L, CurTok);
    if (!LHS)
      return false;

    if (CurTok != ',') {
      LogErrorV("Expected ',' after LHS of cmp", L.getLine());
      return false;
    }
    CurTok = L.get_token();

    auto RHS = P.ParseExpression(L, CurTok);
    if (!RHS)
      return false;

    Value *LVal = LHS->codegen(Ctx);
    Value *RVal = RHS->codegen(Ctx);
    if (!LVal || !RVal)
      return false;

    LVal = createCast(Ctx.Builder, LVal, Ctx.Builder.getInt32Ty());
    RVal = createCast(Ctx.Builder, RVal, Ctx.Builder.getInt32Ty());

    Value *Diff = Ctx.Builder.CreateSub(LVal, RVal, "cmptmp");
    Ctx.Builder.CreateStore(Diff, Ctx.CmpVal);
    return true;
  }

  bool Compile() {
    bool HasReturn = false;

    // Map entry block
    Ctx.LabelBlocks["entry"] = &Ctx.MainFn->getEntryBlock();

    // Create CMP Register
    Ctx.CmpVal =
        Ctx.CreateEntryBlockAlloca("__cmp_reg", Ctx.Builder.getInt32Ty());
    Ctx.Builder.CreateStore(Ctx.Builder.getInt32(1), Ctx.CmpVal);

    while (CurTok != tok_eof) {
      if (CurTok == tok_let) {
        if (!HandleLet())
          return false;
      } else if (CurTok == tok_asm) {
        if (!HandleAsm())
          return false;
      } else if (CurTok == tok_return) {
        if (!HandleReturn(HasReturn))
          return false;
      } else if (CurTok == tok_label) {
        if (!HandleLabel())
          return false;
      } else if (CurTok == tok_br) {
        if (!HandleBr())
          return false;
      } else if (CurTok == tok_cmp) {
        if (!HandleCmp())
          return false;
      } else {
        CurTok = L.get_token();
      }
    }

    if (!Ctx.Builder.GetInsertBlock()->getTerminator())
      Ctx.Builder.CreateRet(ConstantInt::get(Ctx.Builder.getInt32Ty(), 0));
    return true;
  }
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    errs() << "Usage: " << argv[0] << " <source_file>\n";
    return 1;
  }

  std::filesystem::path filePath(argv[1]);
  if (!std::filesystem::exists(filePath)) {
    errs() << "Error: File '" << argv[1] << "' not found.\n";
    return 1;
  }

  std::ifstream ifs(filePath);
  if (!ifs) {
    errs() << "Error: Could not open file " << argv[1] << "\n";
    return 1;
  }
  std::string Content((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());

  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  std::string TargetTriple = sys::getDefaultTargetTriple();
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TargetTriple, Error);
  if (!TheTarget) {
    errs() << "Error: " << Error;
    return 1;
  }

  std::string CPU = sys::getHostCPUName().str();
  TargetOptions opt;
  auto RM = std::optional<Reloc::Model>();
  auto TargetMachine = std::unique_ptr<class TargetMachine>(
      TheTarget->createTargetMachine(TargetTriple, CPU, "", opt, RM));

  LLVMContext Context;
  auto M = std::make_unique<Module>("MyCompiler", Context);
  M->setTargetTriple(TargetTriple);
  M->setDataLayout(TargetMachine->createDataLayout());

  IRBuilder<> Builder(Context);
  CodeGenContext Ctx(Context, M.get(), Builder, M->getDataLayout());

  Lexer L(Content);
  Parser P;
  Compiler Comp(L, P, Ctx);

  // Create main function
  FunctionType *FT = FunctionType::get(Builder.getInt32Ty(), false);
  Function *MainFn =
      Function::Create(FT, Function::ExternalLinkage, "main", M.get());
  BasicBlock *BB = BasicBlock::Create(Context, "entry", MainFn);
  Builder.SetInsertPoint(BB);

  Ctx.MainFn = MainFn;

  if (!Comp.Compile()) {
    return 1;
  }

  // --- [New] Verify the entire module ---
  if (verifyModule(*M, &errs())) {
    errs() << "Error: Module verification failed!\n";
    return 1;
  }

  M->print(outs(), nullptr);

  return 0;
}