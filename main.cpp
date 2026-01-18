#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <fstream>
using namespace llvm;

// --- [1. 렉서 파트] ---
enum Token {
  tok_eof = -1,
  tok_let = -2,
  tok_identifier = -3,
  tok_number = -4,
  tok_asm = -5,
  tok_string = -6,
  tok_return = -7,
};

struct VariableInfo {
  Value *Address;
  Type *VarType;
};

static double NumVal;
static std::string IdentifierStr;
static int LastChar = ' ';
static std::vector<char> FileContent;
static size_t CurrentPos = 0;
static int next_char() {
  if (CurrentPos >= FileContent.size()) return EOF;
  return FileContent[CurrentPos++];
}
static int get_token() {
  while (isspace(LastChar)) LastChar = next_char();

  // 식별자 및 키워드 처리
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = next_char()))) IdentifierStr += LastChar;
    if (IdentifierStr == "let") return tok_let;
    if (IdentifierStr == "asm") return tok_asm;
    if (IdentifierStr == "return") return tok_return;
    return tok_identifier;
  }

  // 숫자 처리
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = next_char();
    } while (isdigit(LastChar) || LastChar == '.');
    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }

  // 문자열 처리 (getchar를 next_char로 수정)
  if (LastChar == '"') {
    IdentifierStr = "";
    while ((LastChar = next_char()) != '"' && LastChar != EOF)
      IdentifierStr += LastChar;
    LastChar = next_char(); // 닫는 따옴표 소비
    return tok_string;
  }

  if (LastChar == EOF) return tok_eof;

  // 일반 문자 처리 (getchar를 next_char로 수정)
  int ThisChar = LastChar;
  LastChar = next_char();
  return ThisChar;
}
// --- [2. 파서 클래스 정의] ---
static std::map<char, int> BinopPrecedence;

static int GetTokPrecedence(int token) {
  if (!isascii(token)) return -1;
  int TokPrec = BinopPrecedence[token];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}

class PARSER {
 public:
  std::map<std::string, VariableInfo> NamedValues;

  Type *getLLVMType(std::string TypeName, LLVMContext &Context) {
    if (TypeName == "i32") return Type::getInt32Ty(Context);
    if (TypeName == "i64") return Type::getInt64Ty(Context);
    if (TypeName == "double") return Type::getDoubleTy(Context);
    return nullptr;
  }

  Value *ParseNumber(IRBuilder<> &Builder, int &CurTok, Type *ExpectedType) {
    Value *V;
    if (ExpectedType && ExpectedType->isDoubleTy()) {
      V = ConstantFP::get(ExpectedType, NumVal);
    } else if (ExpectedType && ExpectedType->isIntegerTy(64)) {
      V = ConstantInt::get(ExpectedType, (int64_t)NumVal);
    } else {
      V = ConstantInt::get(ExpectedType ? ExpectedType : Builder.getInt32Ty(),
                           (int32_t)NumVal);
    }
    CurTok = get_token();
    return V;
  }

  Value *ParsePrimary(IRBuilder<> &Builder, int &CurTok, Type *ExpectedType) {
    if (CurTok == tok_number) {
      return ParseNumber(Builder, CurTok, ExpectedType);
    } else if (CurTok == tok_identifier) {
      std::string Name = IdentifierStr;
      CurTok = get_token();
      if (NamedValues.find(Name) == NamedValues.end()) {
        std::cerr << "오류: 알 수 없는 변수 " << Name << std::endl;
        return nullptr;
      }
      VariableInfo info = NamedValues[Name];
      return Builder.CreateLoad(info.VarType, info.Address, Name.c_str());
    }
    return nullptr;
  }

  Value *ParseBinOpRHS(int ExprPrec, Value *LHS, IRBuilder<> &Builder,
                       int &CurTok, Type *ExpectedType) {
    while (true) {
      int TokPrec = GetTokPrecedence(CurTok);
      if (TokPrec < ExprPrec) return LHS;

      int BinOp = CurTok;
      CurTok = get_token();
      Value *RHS = ParsePrimary(Builder, CurTok, ExpectedType);

      int NextPrec = GetTokPrecedence(CurTok);
      if (TokPrec < NextPrec) {
        RHS = ParseBinOpRHS(TokPrec + 1, RHS, Builder, CurTok, ExpectedType);
      }

      // 타입에 따른 연산자 분기 (정수 vs 실수)
      bool isDouble = ExpectedType && ExpectedType->isDoubleTy();
      if (BinOp == '+')
        LHS = isDouble ? Builder.CreateFAdd(LHS, RHS, "addtmp")
                       : Builder.CreateAdd(LHS, RHS, "addtmp");
      else if (BinOp == '-')
        LHS = isDouble ? Builder.CreateFSub(LHS, RHS, "subtmp")
                       : Builder.CreateSub(LHS, RHS, "subtmp");
      else if (BinOp == '*')
        LHS = isDouble ? Builder.CreateFMul(LHS, RHS, "multmp")
                       : Builder.CreateMul(LHS, RHS, "multmp");
    }
  }

  Value *ParseAsm(IRBuilder<> &Builder, int &CurTok, Module *M) {
    CurTok = get_token();
    if (CurTok != tok_string) return nullptr;

    std::string AsmCode = IdentifierStr;
    CurTok = get_token();

    std::vector<Value *> Args;
    std::string Constraints = "";

    if (CurTok == '(') {
      CurTok = get_token();
      if (CurTok == tok_identifier) {
        std::string VarName = IdentifierStr;
        if (NamedValues.count(VarName)) {
          VariableInfo &info = NamedValues[VarName];
          Value *LoadedVal =
              Builder.CreateLoad(info.VarType, info.Address, VarName.c_str());
          Args.push_back(LoadedVal);
          Constraints = "r";
        }
        CurTok = get_token();
      }
      if (CurTok == ')') CurTok = get_token();
    }

    std::vector<Type *> ArgTypes;
    for (Value *Arg : Args) ArgTypes.push_back(Arg->getType());
    FunctionType *FT = FunctionType::get(Builder.getVoidTy(), ArgTypes, false);
    InlineAsm *IA = InlineAsm::get(FT, AsmCode, Constraints, true);
    return Builder.CreateCall(FT, IA, Args);
  }
};

// --- [3. 메인 엔진] ---
int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "사용법: " << argv[0] << " <소스파일>" << std::endl;
    return 1;
  }

  // 1. 파일 읽기 및 메모리 적재
  std::ifstream ifs(argv[1], std::ios::binary | std::ios::ate);
  if (!ifs) {
    std::cerr << "파일을 열 수 없습니다." << std::endl;
    return 1;
  }
  std::streamsize size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  FileContent.resize(size);
  if (!ifs.read(FileContent.data(), size)) return 1;

  // 2. 초기화
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  LLVMContext Context;
  Module *M = new Module("MyCoollang", Context);
  IRBuilder<> Builder(Context);
  PARSER parser;

  FunctionType *FT = FunctionType::get(Builder.getInt32Ty(), false);
  Function *MainFn = Function::Create(FT, Function::ExternalLinkage, "main", M);
  BasicBlock *BB = BasicBlock::Create(Context, "entry", MainFn);
  Builder.SetInsertPoint(BB);

  // 3. 파일 파싱 루프 (REPL 제거)
  int CurTok = get_token();
  while (CurTok != tok_eof) {
    if (CurTok == tok_let) {
      CurTok = get_token();
      std::string TypeName = IdentifierStr;
      Type *VarType = parser.getLLVMType(TypeName, Context);

      if (!VarType) {
        std::cerr << "오류: 알 수 없는 타입 " << TypeName << std::endl;
        CurTok = get_token();
        continue;
      }

      CurTok = get_token(); // 타입명 소비
      std::string Name = IdentifierStr;
      CurTok = get_token(); // 변수명 소비

      Value *LHS = parser.ParsePrimary(Builder, CurTok, VarType);
      if (LHS) {
        Value *FinalVal = parser.ParseBinOpRHS(0, LHS, Builder, CurTok, VarType);
        AllocaInst *Alloca = Builder.CreateAlloca(VarType, nullptr, Name);
        Builder.CreateStore(FinalVal, Alloca);
        parser.NamedValues[Name] = {Alloca, VarType};
      }
    } else if (CurTok == tok_asm) {
      parser.ParseAsm(Builder, CurTok, M);
    } else if (CurTok == tok_return) {
      CurTok = get_token();
      Type *RetType = Builder.getInt32Ty();
      Value *LHS = parser.ParsePrimary(Builder, CurTok, RetType);
      if (LHS) {
        Value *RetVal = parser.ParseBinOpRHS(0, LHS, Builder, CurTok, RetType);
        Builder.CreateRet(RetVal);
      }
    } else {
      CurTok = get_token(); // 알 수 없는 토큰 건너뛰기
    }
  }

  // 4. IR 결과 출력 (파일로 저장)
  std::error_code EC;
  raw_fd_ostream dest("output.ll", EC);
  if (EC) {
    std::cerr << "파일을 쓸 수 없습니다: " << EC.message() << std::endl;
    return 1;
  }
  M->print(dest, nullptr);
  M->print(outs(), nullptr); // 화면에도 출력

  std::cout << "\n--- 성공: output.ll 파일이 생성되었습니다. ---" << std::endl;
  return 0;
}
