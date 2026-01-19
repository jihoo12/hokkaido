#include <cstdio>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h> // 타겟 초기화 관련
#include <llvm/MC/TargetRegistry.h>    // 타겟 레지스트리
#include <llvm/Target/TargetMachine.h> // 타겟 머신
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h> // 호스트 시스템 정보
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/InlineAsm.h>

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <optional>

using namespace llvm;

// --- [1. 토큰 및 렉서] ---
enum Token
{
    tok_eof = -1,
    tok_let = -2,
    tok_identifier = -3,
    tok_number = -4,
    tok_asm = -5,
    tok_string = -6,
    tok_return = -7,
};

class Lexer
{
public:
    double NumVal;
    std::string IdentifierStr;
    Lexer(const std::vector<char> &content) : FileContent(content), CurrentPos(0), LastChar(' ') {}

    int get_token()
    {
        while (isspace(LastChar))
            LastChar = next_char();
        if (isalpha(LastChar))
        {
            IdentifierStr = LastChar;
            while (isalnum((LastChar = next_char())))
                IdentifierStr += LastChar;
            if (IdentifierStr == "let")
                return tok_let;
            if (IdentifierStr == "asm")
                return tok_asm;
            if (IdentifierStr == "return")
                return tok_return;
            return tok_identifier;
        }
        if (isdigit(LastChar) || LastChar == '.')
        {
            std::string NumStr;
            do
            {
                NumStr += LastChar;
                LastChar = next_char();
            } while (isdigit(LastChar) || LastChar == '.');
            NumVal = strtod(NumStr.c_str(), nullptr);
            return tok_number;
        }
        if (LastChar == '"')
        {
            IdentifierStr = "";
            while ((LastChar = next_char()) != '"' && LastChar != EOF)
                IdentifierStr += LastChar;
            LastChar = next_char();
            return tok_string;
        }
        if (LastChar == EOF)
            return tok_eof;
        int ThisChar = LastChar;
        LastChar = next_char();
        return ThisChar;
    }

private:
    const std::vector<char> &FileContent;
    size_t CurrentPos;
    int LastChar;
    int next_char() { return (CurrentPos >= FileContent.size()) ? EOF : FileContent[CurrentPos++]; }
};

struct VariableInfo
{
    Value *Address;
    Type *VarType;
};

// --- [2. AST (Abstract Syntax Tree) 정의] ---

class ExprAST
{
public:
    virtual ~ExprAST() = default;
    virtual Value *codegen(IRBuilder<> &Builder, std::map<std::string, VariableInfo> &NamedValues) = 0;
};

class NumberExprAST : public ExprAST
{
    double Val;
    Type *Ty;

public:
    NumberExprAST(double Val, Type *Ty) : Val(Val), Ty(Ty) {}
    Value *codegen(IRBuilder<> &Builder, std::map<std::string, VariableInfo> &NamedValues) override
    {
        if (Ty && Ty->isDoubleTy())
            return ConstantFP::get(Ty, Val);
        return ConstantInt::get(Ty ? Ty : Builder.getInt32Ty(), (int64_t)Val);
    }
};

class VariableExprAST : public ExprAST
{
    std::string Name;

public:
    VariableExprAST(std::string Name) : Name(Name) {}
    Value *codegen(IRBuilder<> &Builder, std::map<std::string, VariableInfo> &NamedValues) override
    {
        if (NamedValues.find(Name) == NamedValues.end())
            return nullptr;
        auto &info = NamedValues[Name];
        return Builder.CreateLoad(info.VarType, info.Address, Name.c_str());
    }
};

class BinaryExprAST : public ExprAST
{
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;
    Type *Ty;

public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS, Type *Ty)
        : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)), Ty(Ty) {}
    Value *codegen(IRBuilder<> &Builder, std::map<std::string, VariableInfo> &NamedValues) override
    {
        Value *L = LHS->codegen(Builder, NamedValues);
        Value *R = RHS->codegen(Builder, NamedValues);
        if (!L || !R)
            return nullptr;
        bool isDouble = Ty && Ty->isDoubleTy();
        switch (Op)
        {
        case '+':
            return isDouble ? Builder.CreateFAdd(L, R, "addtmp") : Builder.CreateAdd(L, R, "addtmp");
        case '-':
            return isDouble ? Builder.CreateFSub(L, R, "subtmp") : Builder.CreateSub(L, R, "subtmp");
        case '*':
            return isDouble ? Builder.CreateFMul(L, R, "multmp") : Builder.CreateMul(L, R, "multmp");
        default:
            return nullptr;
        }
    }
};

class AsmExprAST : public ExprAST {
    std::string AsmString;

public:
    AsmExprAST(const std::string &Str) : AsmString(Str) {}

    Value *codegen(IRBuilder<> &Builder, std::map<std::string, VariableInfo> &NamedValues) override {
        // 1. 함수 타입 정의: 입력도 없고 출력도 없는 void() 타입으로 가정
        // (복잡한 입출력이 필요하면 이 부분을 수정해야 함)
        FunctionType *FTy = FunctionType::get(Builder.getVoidTy(), false);

        // 2. 인라인 어셈블리 객체 생성
        // 파라미터: 함수타입, 어셈블리코드, 제약조건(Constraints), SideEffects 여부
        // "NOP" 같은 명령은 SideEffects=true로 설정해야 최적화 과정에서 삭제되지 않음
        InlineAsm *IA = InlineAsm::get(FTy, AsmString, "", true);

        // 3. Call 명령어 생성하여 삽입
        return Builder.CreateCall(IA);
    }
};
// --- [3. 파서 클래스] ---

class Parser
{
    std::map<char, int> BinopPrecedence;

public:
    Parser()
    {
        BinopPrecedence['+'] = 20;
        BinopPrecedence['-'] = 20;
        BinopPrecedence['*'] = 40;
    }

    int GetTokPrecedence(int Tok)
    {
        if (!isascii(Tok))
            return -1;
        int Prec = BinopPrecedence[Tok];
        return Prec <= 0 ? -1 : Prec;
    }

    std::unique_ptr<ExprAST> ParsePrimary(Lexer &L, int &CurTok, Type *Ty)
    {
        if (CurTok == tok_number)
        {
            auto Res = std::make_unique<NumberExprAST>(L.NumVal, Ty);
            CurTok = L.get_token();
            return Res;
        }
        if (CurTok == tok_identifier)
        {
            auto Res = std::make_unique<VariableExprAST>(L.IdentifierStr);
            CurTok = L.get_token();
            return Res;
        }
        return nullptr;
    }

    std::unique_ptr<ExprAST> ParseBinOpRHS(Lexer &L, int ExprPrec, std::unique_ptr<ExprAST> LHS, int &CurTok, Type *Ty)
    {
        while (true)
        {
            int TokPrec = GetTokPrecedence(CurTok);
            if (TokPrec < ExprPrec)
                return LHS;
            int BinOp = CurTok;
            CurTok = L.get_token();
            auto RHS = ParsePrimary(L, CurTok, Ty);
            if (!RHS)
                return nullptr;
            int NextPrec = GetTokPrecedence(CurTok);
            if (TokPrec < NextPrec)
            {
                RHS = ParseBinOpRHS(L, TokPrec + 1, std::move(RHS), CurTok, Ty);
                if (!RHS)
                    return nullptr;
            }
            LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS), Ty);
        }
    }

    std::unique_ptr<ExprAST> ParseAsm(Lexer &L, int &CurTok) {
        // 현재 CurTok은 'tok_asm' 상태임
        CurTok = L.get_token(); // 'asm' 토큰 소모 -> 다음 토큰(문자열)으로 이동

        if (CurTok != tok_string) {
            std::cerr << "Error: asm 키워드 뒤에는 문자열이 와야 합니다." << std::endl;
            return nullptr;
        }

        std::string AsmCode = L.IdentifierStr;
        CurTok = L.get_token(); // 문자열 토큰 소모 -> 다음으로 이동 (예: ';')
        
        return std::make_unique<AsmExprAST>(AsmCode);
    }

    Type *getLLVMType(std::string Name, LLVMContext &Ctx)
    {
        if (Name == "i32")
            return Type::getInt32Ty(Ctx);
        if (Name == "i64")
            return Type::getInt64Ty(Ctx);
        if (Name == "double")
            return Type::getDoubleTy(Ctx);
        return nullptr;
    }

    static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, StringRef VarName, Type *VarType)
    {
        // 함수의 진입 블록(첫 번째 블록)을 가리키는 Builder를 임시로 생성합니다.
        IRBuilder<> TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
        return TmpB.CreateAlloca(VarType, nullptr, VarName);
    }
};

// --- [4. 메인 엔진] ---

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "사용법: " << argv[0] << " <소스파일>" << std::endl;
        return 1;
    }

    // 1. 소스 파일 읽기
    std::ifstream ifs(argv[1], std::ios::binary | std::ios::ate);
    if (!ifs)
        return 1;
    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<char> Content(size);
    ifs.read(Content.data(), size);

    // 2. LLVM 타겟 초기화
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();

    // 3. 호스트 시스템 타겟 트리플 설정
    std::string TargetTriple = sys::getDefaultTargetTriple();

    // 4. 타겟 머신 생성 (수정된 부분)
    std::string Error;
    const Target *TheTarget = TargetRegistry::lookupTarget(TargetTriple, Error);
    if (!TheTarget)
    {
        errs() << "Error: " << Error;
        return 1;
    }

    TargetOptions opt;
    // LLVM 최신 버전에서는 std::optional을 사용하며,
    // Reloc::Model은 직접 타입으로 명시합니다.
    std::optional<Reloc::Model> RM = std::nullopt;

    auto TargetMachine = TheTarget->createTargetMachine(TargetTriple, "generic", "", opt, RM);

    // 5. LLVM 컨텍스트 및 모듈 생성
    LLVMContext Context;
    auto M = std::make_unique<Module>("MyCompiler", Context);

    // 6. 모듈에 타겟 정보 및 DataLayout 주입
    M->setTargetTriple(TargetTriple);
    M->setDataLayout(TargetMachine->createDataLayout());
    auto &DL = M->getDataLayout();

    IRBuilder<> Builder(Context);
    Lexer L(Content);
    Parser P; // 앞서 정의한 AST 기반 Parser
    std::map<std::string, VariableInfo> NamedValues;

    // 7. 기본 함수(main) 생성
    FunctionType *FT = FunctionType::get(Builder.getInt32Ty(), false);
    Function *MainFn = Function::Create(FT, Function::ExternalLinkage, "main", M.get());
    BasicBlock *BB = BasicBlock::Create(Context, "entry", MainFn);
    Builder.SetInsertPoint(BB);

    // 8. 파싱 루프 (생략된 Parser 로직 사용)
    int CurTok = L.get_token();
    while (CurTok != tok_eof)
    {
        if (CurTok == tok_let)
        {
            CurTok = L.get_token(); // Type
            Type *VarType = P.getLLVMType(L.IdentifierStr, Context);
            CurTok = L.get_token(); // Name
            std::string VarName = L.IdentifierStr;
            CurTok = L.get_token(); // Move to next

            auto LHS = P.ParsePrimary(L, CurTok, VarType);
            auto FullAST = P.ParseBinOpRHS(L, 0, std::move(LHS), CurTok, VarType);

            if (FullAST)
            {
                Value *Val = FullAST->codegen(Builder, NamedValues);

                // 1. 함수의 진입 지점에 Alloca 생성
                AllocaInst *Alloca = Parser::CreateEntryBlockAlloca(MainFn, VarName, VarType);

                // 2. 정렬(Alignment) 설정 (DataLayout 활용)
                Align PreferredAlign = DL.getPrefTypeAlign(VarType);
                Alloca->setAlignment(PreferredAlign);

                // 3. 현재 위치(Builder의 현재 포인트)에서 Store 수행
                Builder.CreateStore(Val, Alloca)->setAlignment(PreferredAlign);

                // 4. 심볼 테이블에 저장
                NamedValues[VarName] = {Alloca, VarType};
            }
        }
        else if (CurTok == tok_asm)
        {
            auto AsmAST = P.ParseAsm(L, CurTok);
            if (AsmAST) {
                AsmAST->codegen(Builder, NamedValues);
            }
            
            // 세미콜론 처리 (선택사항: 코드 안정성을 위해)
            if (CurTok == ';') CurTok = L.get_token();
        }
        else if (CurTok == tok_return)
        {
            CurTok = L.get_token();
            auto LHS = P.ParsePrimary(L, CurTok, Builder.getInt32Ty());
            auto FullAST = P.ParseBinOpRHS(L, 0, std::move(LHS), CurTok, Builder.getInt32Ty());
            if (FullAST)
                Builder.CreateRet(FullAST->codegen(Builder, NamedValues));
        }
        else
        {
            CurTok = L.get_token();
        }
    }

    // 9. 검증 및 IR 출력
    verifyFunction(*MainFn);
    M->print(outs(), nullptr);

    return 0;
}
