#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>  // 1. 필수 헤더 추가
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <map>
#include <string>
#include <vector>

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

static double NumVal;
static std::string IdentifierStr;
static int LastChar = ' ';

static int get_token() {
  while (isspace(LastChar)) LastChar = getchar();
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar()))) IdentifierStr += LastChar;
    if (IdentifierStr == "let") return tok_let;
    if (IdentifierStr == "asm") return tok_asm;  // asm 키워드 체크 추가
    if (IdentifierStr == "return") return tok_return;
    return tok_identifier;
  }
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');
    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }
  if (LastChar == '"') {
    IdentifierStr = "";
    while ((LastChar = getchar()) != '"' && LastChar != EOF)
      IdentifierStr += LastChar;
    LastChar = getchar();
    return tok_string;
  }
  if (LastChar == EOF) return tok_eof;
  int ThisChar = LastChar;
  LastChar = getchar();
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
  std::map<std::string, Value *> NamedValues;

  Value *ParseNumber(IRBuilder<> &Builder, int &CurTok);
  Value *ParsePrimary(IRBuilder<> &Builder, int &CurTok);
  Value *ParseBinOpRHS(int ExprPrec, Value *LHS, IRBuilder<> &Builder,
                       int &CurTok);
  // 2. 클래스 내부 선언에서 PARSER:: 제거
  Value *ParseAsm(IRBuilder<> &Builder, int &CurTok, Module *M);
};

Value *PARSER::ParseNumber(IRBuilder<> &Builder, int &CurTok) {
  Value *V = Builder.getInt32((int)NumVal);
  CurTok = get_token();
  return V;
}

Value *PARSER::ParsePrimary(IRBuilder<> &Builder, int &CurTok) {
  if (CurTok == tok_number) {
    return ParseNumber(Builder, CurTok);
  } else if (CurTok == tok_identifier) {
    std::string Name = IdentifierStr;
    CurTok = get_token();
    Value *V = NamedValues[Name];
    if (!V) {
      std::cerr << "오류: 알 수 없는 변수 이름 " << Name << std::endl;
      return nullptr;
    }
    return Builder.CreateLoad(Builder.getInt32Ty(), V, Name.c_str());
  }
  return nullptr;
}

Value *PARSER::ParseBinOpRHS(int ExprPrec, Value *LHS, IRBuilder<> &Builder,
                             int &CurTok) {
  while (true) {
    int TokPrec = GetTokPrecedence(CurTok);
    if (TokPrec < ExprPrec) return LHS;

    int BinOp = CurTok;
    CurTok = get_token();
    Value *RHS = ParsePrimary(Builder, CurTok);

    int NextPrec = GetTokPrecedence(CurTok);
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, RHS, Builder, CurTok);
    }

    if (BinOp == '+')
      LHS = Builder.CreateAdd(LHS, RHS, "addtmp");
    else if (BinOp == '-')
      LHS = Builder.CreateSub(LHS, RHS, "subtmp");
    else if (BinOp == '*')
      LHS = Builder.CreateMul(LHS, RHS, "multmp");
  }
}

Value *PARSER::ParseAsm(IRBuilder<> &Builder, int &CurTok, Module *M) {
  CurTok = get_token();  // asm 건너뛰기
  if (CurTok != tok_string) {
    std::cerr << "오류: asm 뒤에는 문자열이 와야 합니다." << std::endl;
    return nullptr;
  }

  std::string AsmCode = IdentifierStr;
  CurTok = get_token();  // 문자열 소비

  std::vector<Value *> Args;
  std::string Constraints = "";

  // 만약 '(' 가 온다면 인자가 있다는 뜻
  if (CurTok == '(') {
    CurTok = get_token();  // '(' 소비
    if (CurTok == tok_identifier) {
      std::string VarName = IdentifierStr;
      Value *V = NamedValues[VarName];
      if (V) {
        // 변수의 값을 로드해서 Args에 추가
        Value *LoadedVal =
            Builder.CreateLoad(Builder.getInt32Ty(), V, VarName.c_str());
        Args.push_back(LoadedVal);
        Constraints = "r";  // "r"은 이 값을 레지스터에 넣어서 전달하라는 뜻
      }
      CurTok = get_token();  // 변수명 소비
    }
    if (CurTok == ')') CurTok = get_token();  // ')' 소비
  }

  // 인자들의 타입 리스트 생성
  std::vector<Type *> ArgTypes;
  for (Value *Arg : Args) ArgTypes.push_back(Arg->getType());

  // 함수 타입 정의 (리턴 void, 인자들 타입)
  FunctionType *FT = FunctionType::get(Builder.getVoidTy(), ArgTypes, false);

  // InlineAsm 객체 생성 (Constraints 적용)
  InlineAsm *IA = InlineAsm::get(FT, AsmCode, Constraints, true);

  // 호출 생성 (인자 Args 전달)
  return Builder.CreateCall(FT, IA, Args);
}
// --- [3. 메인 엔진] ---
int main() {
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

  std::cout << "MyCoollang REPL (Ctrl+D 또는 'exit' 입력 시 종료)" << std::endl;

  while (true) {
    std::cout << "ready> ";
    int CurTok = get_token();
    if (CurTok == tok_eof) break;

    if (CurTok == tok_let) {
      CurTok = get_token();
      std::string Name = IdentifierStr;
      CurTok = get_token();
      Value *LHS = parser.ParsePrimary(Builder, CurTok);
      if (!LHS) continue;
      Value *FinalVal = parser.ParseBinOpRHS(0, LHS, Builder, CurTok);
      AllocaInst *Alloca =
          Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, Name);
      Builder.CreateStore(FinalVal, Alloca);
      parser.NamedValues[Name] = Alloca;
      std::cout << "Defined variable: " << Name << std::endl;
    } else if (CurTok == tok_asm) {
      parser.ParseAsm(Builder, CurTok, M);
      std::cout << "Inline ASM block added." << std::endl;
    } else if (CurTok == tok_return) {
      CurTok = get_token();  // return 건너뛰기

      // 뒤에 오는 수식(예: a + 5) 파싱
      Value *LHS = parser.ParsePrimary(Builder, CurTok);
      if (LHS) {
        Value *RetVal = parser.ParseBinOpRHS(0, LHS, Builder, CurTok);
        Builder.CreateRet(RetVal);  // 사용자 정의 값으로 return 생성
        std::cout << "Return statement added." << std::endl;
      }
    } else {
      if (IdentifierStr == "exit") break;
      // 알 수 없는 문자는 그냥 건너뜀 (예: 세미콜론)
    }
  }
  std::cout << "\n--- 최종 생성된 LLVM IR ---" << std::endl;
  M->print(outs(), nullptr);
  return 0;
}
