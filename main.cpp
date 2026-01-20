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

class ExprAST; // Forward declaration

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

  // Fallback / Error case handling could be added here
  // For now, let LLVM verify catch invalid casts if strictly other types
  return Val;
}

std::unique_ptr<ExprAST> LogError(const char *Str) {
  std::cerr << "Error: " << Str << std::endl;
  return nullptr;
}

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

// Helper to convert GCC-style asm syntax to LLVM style
// %0, %1 -> $0, $1
// $10 -> $$10 (escaped immediate)
std::string ConvertAsmString(const std::string &In) {
  std::string Out;
  for (size_t i = 0; i < In.size(); ++i) {
    if (In[i] == '%') {
      if (i + 1 < In.size() && isdigit(In[i + 1])) {
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

// --- [1. Token & Lexer] ---

enum Token {
  tok_eof = -1,
  tok_let = -2,
  tok_identifier = -3,
  tok_number = -4,
  tok_asm = -5,
  tok_string = -6,
  tok_return = -7,
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
    while (isspace(LastChar))
      LastChar = next_char();

    if (isalpha(LastChar)) {
      IdentifierStr = LastChar;
      while (isalnum((LastChar = next_char())) || LastChar == '_')
        IdentifierStr += LastChar;

      if (IdentifierStr == "let")
        return tok_let;
      if (IdentifierStr == "asm")
        return tok_asm;
      if (IdentifierStr == "return")
        return tok_return;
      return tok_identifier;
    }

    if (isdigit(LastChar) || LastChar == '.') {
      std::string NumStr;
      do {
        NumStr += LastChar;
        LastChar = next_char();
      } while (isdigit(LastChar) || LastChar == '.');

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
      LastChar = next_char(); // eat closing quote
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

struct VariableInfo {
  Value *Address;
  Type *VarType;
};

// --- [2. AST] ---

class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen(IRBuilder<> &Builder,
                         std::map<std::string, VariableInfo> &NamedValues,
                         const DataLayout &DL) = 0;
};

class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}

  Value *codegen(IRBuilder<> &Builder,
                 std::map<std::string, VariableInfo> &NamedValues,
                 const DataLayout &DL) override {
    // Default to double, but context might cast it later.
    // However, traditionally numbers are doubles in this toy lang unless
    // specified otherwise. We will return a Double constant. The caller (BinOp
    // or Assigment) handles casting.
    return ConstantFP::get(Builder.getDoubleTy(), Val);
  }
};

class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(std::string Name) : Name(std::move(Name)) {}

  Value *codegen(IRBuilder<> &Builder,
                 std::map<std::string, VariableInfo> &NamedValues,
                 const DataLayout &DL) override {
    if (NamedValues.find(Name) == NamedValues.end())
      return LogErrorV(("Unknown variable name: " + Name).c_str());

    auto &info = NamedValues[Name];
    auto *Load = Builder.CreateLoad(info.VarType, info.Address, Name.c_str());
    Load->setAlignment(DL.getPrefTypeAlign(info.VarType));
    return Load;
  }
};

class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

  Value *codegen(IRBuilder<> &Builder,
                 std::map<std::string, VariableInfo> &NamedValues,
                 const DataLayout &DL) override {
    Value *L = LHS->codegen(Builder, NamedValues, DL);
    Value *R = RHS->codegen(Builder, NamedValues, DL);
    if (!L || !R)
      return nullptr;

    // Type Promotion Logic
    Type *TyL = L->getType();
    Type *TyR = R->getType();

    // If one is double, cast other to double
    if (TyL->isDoubleTy() || TyR->isDoubleTy()) {
      L = createCast(Builder, L, Builder.getDoubleTy());
      R = createCast(Builder, R, Builder.getDoubleTy());
    } else {
      // Both ints (default fallback)
      L = createCast(Builder, L, Builder.getInt32Ty());
      R = createCast(Builder, R, Builder.getInt32Ty());
    }

    // Now types are same
    bool isDouble = L->getType()->isDoubleTy();

    switch (Op) {
    case '+':
      return isDouble ? Builder.CreateFAdd(L, R, "addtmp")
                      : Builder.CreateAdd(L, R, "addtmp");
    case '-':
      return isDouble ? Builder.CreateFSub(L, R, "subtmp")
                      : Builder.CreateSub(L, R, "subtmp");
    case '*':
      return isDouble ? Builder.CreateFMul(L, R, "multmp")
                      : Builder.CreateMul(L, R, "multmp");
    case '/': // Added div support just in case
      return isDouble ? Builder.CreateFDiv(L, R, "divtmp")
                      : Builder.CreateSDiv(L, R, "divtmp");
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

  Value *codegen(IRBuilder<> &Builder,
                 std::map<std::string, VariableInfo> &NamedValues,
                 const DataLayout &DL) override {
    std::vector<Value *> ArgValues;
    std::vector<Type *> ArgTypes;
    std::string Constraints = "";

    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
      Value *ArgVal = Args[i]->codegen(Builder, NamedValues, DL);
      if (!ArgVal)
        return nullptr;

      // Default constraint for inputs
      if (i > 0)
        Constraints += ",";
      Constraints += "r";

      ArgValues.push_back(ArgVal);
      ArgTypes.push_back(ArgVal->getType());
    }

    // Side effects = true (typical for simple inline asm statements)
    FunctionType *FTy = FunctionType::get(Builder.getVoidTy(), ArgTypes, false);
    InlineAsm *IA = InlineAsm::get(FTy, AsmString, Constraints, true);
    return Builder.CreateCall(IA, ArgValues);
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
    if (!isascii(Tok))
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
      if (CurTok != ')')
        return LogError("expected ')'");
      CurTok = L.get_token();
      return V;
    }
    return LogError("Unknown token when expecting an expression");
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
    if (CurTok != tok_string)
      return LogError("Expected string after asm");

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

  static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                            StringRef VarName, Type *VarType) {
    IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                     TheFunction->getEntryBlock().begin());
    return TmpB.CreateAlloca(VarType, nullptr, VarName);
  }
};

// --- [4. Compiler Engine] ---

bool CompileModule(Lexer &L, Parser &P, IRBuilder<> &Builder, Function &MainFn,
                   std::map<std::string, VariableInfo> &NamedValues,
                   const DataLayout &DL, LLVMContext &Context) {
  int CurTok = L.get_token();
  bool HasReturn = false;

  while (CurTok != tok_eof) {
    if (CurTok == tok_let) {
      // let <Type> <Name> = <Expr>
      CurTok = L.get_token(); // eat let
      std::string TypeName = L.IdentifierStr;
      Type *VarType = P.getLLVMType(TypeName, Context);
      if (!VarType) {
        std::cerr << "Error: Unknown type '" << TypeName << "' around line "
                  << L.getLine() << std::endl;
        return false;
      }

      CurTok = L.get_token();                // eat Type
      std::string VarName = L.IdentifierStr; // Name

      CurTok = L.get_token(); // eat Name
      if (CurTok != '=') {
        std::cerr << "Error: Expected '=' after variable name around line "
                  << L.getLine() << std::endl;
        return false;
      }
      CurTok = L.get_token(); // eat =

      auto Expr = P.ParseExpression(L, CurTok);
      if (Expr) {
        Value *Val = Expr->codegen(Builder, NamedValues, DL);
        if (!Val)
          return false;

        // Critical Fix: Explicit Cast
        Val = createCast(Builder, Val, VarType);

        AllocaInst *Alloca =
            Parser::CreateEntryBlockAlloca(&MainFn, VarName, VarType);
        Align PreferredAlign = DL.getPrefTypeAlign(VarType);
        Alloca->setAlignment(PreferredAlign);
        Builder.CreateStore(Val, Alloca)->setAlignment(PreferredAlign);
        NamedValues[VarName] = {Alloca, VarType};
      }
    } else if (CurTok == tok_asm) {
      auto AsmAST = P.ParseAsm(L, CurTok);
      if (AsmAST) {
        AsmAST->codegen(Builder, NamedValues, DL);
      }
      if (CurTok == ';')
        CurTok = L.get_token();
    } else if (CurTok == tok_return) {
      CurTok = L.get_token();
      auto Expr = P.ParseExpression(L, CurTok);
      if (Expr) {
        Value *retVal = Expr->codegen(Builder, NamedValues, DL);
        // main always returns i32 in this toy lang
        retVal = createCast(Builder, retVal, Builder.getInt32Ty());
        Builder.CreateRet(retVal);
        HasReturn = true;
      }
    } else {
      // Ignore unknown tokens or handle errors
      CurTok = L.get_token();
    }
  }

  if (!HasReturn) {
    Builder.CreateRet(ConstantInt::get(Builder.getInt32Ty(), 0));
  }
  return true;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <source_file>" << std::endl;
    return 1;
  }

  // Modern C++ File Reading
  std::filesystem::path filePath(argv[1]);
  if (!std::filesystem::exists(filePath)) {
    std::cerr << "Error: File '" << argv[1] << "' not found." << std::endl;
    return 1;
  }

  std::ifstream ifs(filePath);
  if (!ifs) {
    std::cerr << "Error: Could not open file " << argv[1] << std::endl;
    return 1;
  }
  std::string Content((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());

  // LLVM Init
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

  TargetOptions opt;
  auto RM = std::optional<Reloc::Model>();
  auto TargetMachine = std::unique_ptr<class TargetMachine>(
      TheTarget->createTargetMachine(TargetTriple, "generic", "", opt, RM));

  LLVMContext Context;
  auto M = std::make_unique<Module>("MyCompiler", Context);
  M->setTargetTriple(TargetTriple);
  M->setDataLayout(TargetMachine->createDataLayout());
  const DataLayout &DL = M->getDataLayout();

  IRBuilder<> Builder(Context);
  Lexer L(Content);
  Parser P;
  std::map<std::string, VariableInfo> NamedValues;

  // Create main
  FunctionType *FT = FunctionType::get(Builder.getInt32Ty(), false);
  Function *MainFn =
      Function::Create(FT, Function::ExternalLinkage, "main", M.get());
  BasicBlock *BB = BasicBlock::Create(Context, "entry", MainFn);
  Builder.SetInsertPoint(BB);

  if (!CompileModule(L, P, Builder, *MainFn, NamedValues, DL, Context)) {
    return 1;
  }

  if (verifyFunction(*MainFn, &errs())) {
    errs() << "Error: Function verification failed!\n";
    // Depending on severity, we might still print M to see what went wrong
  }

  M->print(outs(), nullptr);

  return 0;
}