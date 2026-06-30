// hokkaido — LLVM-based compiler with cubical compile-time evaluation
//
// Syntax:
//   // Regular variable declaration
//   let int x = 42
//   let float y = 3.14
//   let string s = "hello"
//
//   // Cubical variable declaration (inline cubical source)
//   let cubical five = "data Nat = | zero : Nat | suc : Nat -> Nat
//                       def main : Nat = suc (suc (suc (suc (suc zero))))"
//
//   // Cubical variable declaration (from file)
//   let cubical result = "path/to/expression.cub"
//
//   // Print statement
//   print x
//
// The cubical type triggers compile-time evaluation of the cubical
// expression. The result is embedded as an LLVM constant.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
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

// =========================================================================
// Cubical FFI — link against the Rust cubical_c static library
// =========================================================================

extern "C" {
/// Evaluate a cubical source string. Returns a C string (caller must free
/// with cubical_free_string), or nullptr on error.
char *cubical_eval(const char *source);

/// Evaluate a cubical source string and return the result as a 64-bit
/// integer. The expression must evaluate to a natural number (Nat).
/// Returns -1 on error.
int64_t cubical_eval_int(const char *source);

/// Free a string returned by cubical_eval.
void cubical_free_string(char *s);
}

// =========================================================================
// Cubical C++ Wrapper
// =========================================================================

/// A compile-time evaluated cubical expression.
class cubical_value {
  std::string result_;
  bool valid_ = false;

public:
  explicit cubical_value(const std::string &source) {
    char *res = cubical_eval(source.c_str());
    if (res) {
      result_ = res;
      valid_ = true;
      cubical_free_string(res);
    }
  }

  bool valid() const { return valid_; }
  const std::string &str() const { return result_; }

  /// If the result is a natural number, return its value. Returns -1 if
  /// the result is not a Nat or evaluation failed.
  int64_t as_int() const {
    if (!valid_) return -1;
    auto eq_pos = result_.find(" = ");
    if (eq_pos == std::string::npos) return -1;
    std::string nat_str = result_.substr(eq_pos + 3);
    return parse_nat(nat_str);
  }

private:
  int64_t parse_nat(const std::string &s) const {
    std::string t = s;
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (t.empty()) return -1;

    while (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
      t = t.substr(1, t.size() - 2);
      while (!t.empty() && t.front() == ' ') t.erase(0, 1);
      while (!t.empty() && t.back() == ' ') t.pop_back();
    }

    if (t == "zero") return 0;
    if (t.size() >= 4 && t.substr(0, 4) == "suc ") {
      int64_t inner = parse_nat(t.substr(4));
      return (inner >= 0) ? inner + 1 : -1;
    }
    if (t.size() >= 4 && t.substr(0, 4) == "suc(") {
      int64_t inner = parse_nat("(" + t.substr(3));
      return (inner >= 0) ? inner + 1 : -1;
    }
    return -1;
  }
};

// =========================================================================
// Hokkaido Language — Lexer
// =========================================================================

enum TokenType {
  TOK_EOF = -1,
  TOK_LET = -2,
  TOK_CUBICAL = -3,
  TOK_PRINT = -4,
  TOK_INT = -5,
  TOK_FLOAT = -6,
  TOK_STRING = -7,
  TOK_IDENTIFIER = -8,
  TOK_NUMBER = -9,
  TOK_STRING_LITERAL = -10,
  TOK_EQUALS = -11,
  TOK_SEMICOLON = -12,
  TOK_NEWLINE = -13,
};

struct Token {
  TokenType type;
  std::string text;
  double num_val;
  int line;
  int col;
};

class Lexer {
  std::string input;
  size_t pos = 0;
  int line = 1;
  int col = 1;

  char peek() {
    if (pos >= input.size()) return '\0';
    return input[pos];
  }

  char advance() {
    char c = input[pos++];
    if (c == '\n') { line++; col = 1; }
    else { col++; }
    return c;
  }

  void skip_whitespace() {
    while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\r'))
      advance();
  }

  void skip_line_comment() {
    while (pos < input.size() && input[pos] != '\n') advance();
  }

public:
  Lexer(const std::string &src) : input(src) {}

  Token next_token() {
    skip_whitespace();

    if (pos >= input.size()) return {TOK_EOF, "", 0, line, col};

    int l = line, c = col;
    char ch = peek();

    // Line comments
    if (ch == '/' && pos + 1 < input.size() && input[pos + 1] == '/') {
      skip_line_comment();
      return next_token();
    }

    // Newlines (statement separators)
    if (ch == '\n') {
      advance();
      return {TOK_NEWLINE, "\\n", 0, l, c};
    }

    // Semicolons
    if (ch == ';') {
      advance();
      return {TOK_SEMICOLON, ";", 0, l, c};
    }

    // Equals
    if (ch == '=') {
      advance();
      return {TOK_EQUALS, "=", 0, l, c};
    }

    // String literals
    if (ch == '"') {
      return lex_string(l, c);
    }

    // Numbers
    if (std::isdigit(ch) || (ch == '-' && pos + 1 < input.size() && std::isdigit(input[pos + 1]))) {
      return lex_number(l, c);
    }

    // Identifiers and keywords
    if (std::isalpha(ch) || ch == '_') {
      return lex_identifier(l, c);
    }

    // Unknown character
    std::string err = "unexpected character '";
    err += ch;
    err += "'";
    advance();
    return {TOK_EOF, err, 0, l, c};
  }

private:
  Token lex_string(int l, int c) {
    advance(); // skip opening "
    std::string val;
    while (pos < input.size() && peek() != '"') {
      if (peek() == '\\') {
        advance();
        if (peek() == 'n') val += '\n';
        else if (peek() == 't') val += '\t';
        else if (peek() == '"') val += '"';
        else if (peek() == '\\') val += '\\';
        else val += peek();
        advance();
      } else {
        val += advance();
      }
    }
    if (pos >= input.size()) {
      return {TOK_EOF, "unterminated string", 0, l, c};
    }
    advance(); // skip closing "
    return {TOK_STRING_LITERAL, val, 0, l, c};
  }

  Token lex_number(int l, int c) {
    std::string num;
    bool is_float = false;
    if (peek() == '-') { num += advance(); }
    while (pos < input.size() && std::isdigit(peek())) {
      num += advance();
    }
    if (pos < input.size() && peek() == '.') {
      is_float = true;
      num += advance();
      while (pos < input.size() && std::isdigit(peek())) {
        num += advance();
      }
    }
    double val = std::stod(num);
    return {TOK_NUMBER, num, val, l, c};
  }

  Token lex_identifier(int l, int c) {
    std::string id;
    while (pos < input.size() && (std::isalnum(peek()) || peek() == '_' || peek() == '-')) {
      id += advance();
    }

    // Check keywords
    if (id == "let") return {TOK_LET, id, 0, l, c};
    if (id == "cubical") return {TOK_CUBICAL, id, 0, l, c};
    if (id == "print") return {TOK_PRINT, id, 0, l, c};
    if (id == "int") return {TOK_INT, id, 0, l, c};
    if (id == "float") return {TOK_FLOAT, id, 0, l, c};
    if (id == "string") return {TOK_STRING, id, 0, l, c};

    return {TOK_IDENTIFIER, id, 0, l, c};
  }
};

// =========================================================================
// Hokkaido Language — AST
// =========================================================================

enum TypeKind {
  TYPE_INT,
  TYPE_FLOAT,
  TYPE_STRING,
  TYPE_CUBICAL,
};

struct TypeAnnotation {
  TypeKind kind;
};

struct Expr {
  virtual ~Expr() = default;
};

struct NumberExpr : Expr {
  double value;
  NumberExpr(double v) : value(v) {}
};

struct StringExpr : Expr {
  std::string value;
  StringExpr(const std::string &v) : value(v) {}
};

struct IdentExpr : Expr {
  std::string name;
  IdentExpr(const std::string &n) : name(n) {}
};

struct Decl {
  virtual ~Decl() = default;
};

struct LetDecl : Decl {
  TypeAnnotation type_ann;
  std::string name;
  std::unique_ptr<Expr> init_expr;
};

struct PrintDecl : Decl {
  std::string name;
};

// =========================================================================
// Hokkaido Language — Parser
// =========================================================================

class Parser {
  Lexer &lexer;
  Token cur_tok;
  bool has_error = false;
  std::string error_msg;

  void next_token() {
    cur_tok = lexer.next_token();
    // Skip newlines (they're just separators, not significant)
    while (cur_tok.type == TOK_NEWLINE) {
      cur_tok = lexer.next_token();
    }
  }

public:
  Parser(Lexer &lex) : lexer(lex) {
    next_token();
  }

  bool ok() const { return !has_error; }
  const std::string &error() const { return error_msg; }

  /// Parse a complete program: sequence of declarations.
  std::vector<std::unique_ptr<Decl>> parse_program() {
    std::vector<std::unique_ptr<Decl>> decls;
    while (cur_tok.type != TOK_EOF) {
      if (cur_tok.type == TOK_LET) {
        auto decl = parse_let_decl();
        if (decl) decls.push_back(std::move(decl));
        else break;
      } else if (cur_tok.type == TOK_PRINT) {
        auto decl = parse_print_decl();
        if (decl) decls.push_back(std::move(decl));
        else break;
      } else {
        error_msg = "expected 'let' or 'print' at line " +
                    std::to_string(cur_tok.line) + ":" +
                    std::to_string(cur_tok.col);
        has_error = true;
        break;
      }
    }
    return decls;
  }

private:
  std::unique_ptr<LetDecl> parse_let_decl() {
    next_token(); // consume 'let'

    // Parse type annotation
    TypeAnnotation type_ann;
    if (cur_tok.type == TOK_INT) {
      type_ann = {TYPE_INT};
      next_token();
    } else if (cur_tok.type == TOK_FLOAT) {
      type_ann = {TYPE_FLOAT};
      next_token();
    } else if (cur_tok.type == TOK_STRING) {
      type_ann = {TYPE_STRING};
      next_token();
    } else if (cur_tok.type == TOK_CUBICAL) {
      type_ann = {TYPE_CUBICAL};
      next_token();
    } else {
      error_msg = "expected type (int, float, string, cubical) at line " +
                  std::to_string(cur_tok.line) + ":" + std::to_string(cur_tok.col);
      has_error = true;
      return nullptr;
    }

    // Parse variable name
    if (cur_tok.type != TOK_IDENTIFIER) {
      error_msg = "expected variable name at line " +
                  std::to_string(cur_tok.line) + ":" + std::to_string(cur_tok.col);
      has_error = true;
      return nullptr;
    }
    std::string name = cur_tok.text;
    next_token();

    // Parse '='
    if (cur_tok.type != TOK_EQUALS) {
      error_msg = "expected '=' at line " +
                  std::to_string(cur_tok.line) + ":" + std::to_string(cur_tok.col);
      has_error = true;
      return nullptr;
    }
    next_token();

    // Parse initializer expression
    std::unique_ptr<Expr> init;
    if (cur_tok.type == TOK_NUMBER) {
      init = std::make_unique<NumberExpr>(cur_tok.num_val);
      next_token();
    } else if (cur_tok.type == TOK_STRING_LITERAL) {
      init = std::make_unique<StringExpr>(cur_tok.text);
      next_token();
    } else if (cur_tok.type == TOK_IDENTIFIER) {
      init = std::make_unique<IdentExpr>(cur_tok.text);
      next_token();
    } else {
      error_msg = "expected expression (number, string, or identifier) at line " +
                  std::to_string(cur_tok.line) + ":" + std::to_string(cur_tok.col);
      has_error = true;
      return nullptr;
    }

    auto decl = std::make_unique<LetDecl>();
    decl->type_ann = type_ann;
    decl->name = name;
    decl->init_expr = std::move(init);
    return decl;
  }

  std::unique_ptr<PrintDecl> parse_print_decl() {
    next_token(); // consume 'print'

    if (cur_tok.type != TOK_IDENTIFIER) {
      error_msg = "expected variable name after 'print' at line " +
                  std::to_string(cur_tok.line) + ":" + std::to_string(cur_tok.col);
      has_error = true;
      return nullptr;
    }
    std::string name = cur_tok.text;
    next_token();

    auto decl = std::make_unique<PrintDecl>();
    decl->name = name;
    return decl;
  }
};

// =========================================================================
// Hokkaido Language — Code Generator
// =========================================================================

class CodeGen {
  LLVMContext &Context;
  Module &M;
  IRBuilder<> &Builder;
  Function *MainFn;
  BasicBlock *EntryBB;

  std::map<std::string, AllocaInst *> named_values;
  std::map<std::string, TypeKind> named_types;

public:
  CodeGen(LLVMContext &Ctx, Module &Mod, IRBuilder<> &Bld)
      : Context(Ctx), M(Mod), Builder(Bld), MainFn(nullptr), EntryBB(nullptr) {}

  bool generate(const std::vector<std::unique_ptr<Decl>> &decls) {
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

private:
  bool gen_let_decl(LetDecl *decl) {
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

  void gen_print_decl(PrintDecl *decl) {
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
};

// =========================================================================
// Main entry point
// =========================================================================

void print_usage() {
  std::cout << "hokkaido — LLVM-based compiler with cubical compile-time evaluation\n\n";
  std::cout << "Usage:\n";
  std::cout << "  my_compiler input.hk          Compile a .hk file\n";
  std::cout << "  my_compiler input.cub          Evaluate a .cub file\n";
  std::cout << "  my_compiler                    Run demo\n\n";
  std::cout << "Hokkaido language syntax:\n";
  std::cout << "  let int x = 42                 Integer variable\n";
  std::cout << "  let float y = 3.14             Float variable\n";
  std::cout << "  let string s = \"hello\"         String variable\n";
  std::cout << "  let cubical n = \"...\"          Cubical inline expression\n";
  std::cout << "  let cubical r = \"file.cub\"     Cubical from file\n";
  std::cout << "  print x                        Print variable\n";
}

void run_demo() {
  std::cout << "=== Hokkaido Compiler Demo ===\n\n";

  // Demo 1: Simple integer
  {
    std::cout << "--- Demo 1: Integer variable ---\n";
    std::string src = R"(
let int x = 42
let int y = x
print x
)";
    LLVMContext Context;
    std::unique_ptr<Module> M = std::make_unique<Module>("hokkaido_demo", Context);
    IRBuilder<> Builder(Context);

    Lexer lexer(src);
    Parser parser(lexer);
    auto decls = parser.parse_program();

    if (parser.ok()) {
      CodeGen cg(Context, *M, Builder);
      if (cg.generate(decls)) {
        std::cout << "\nLLVM IR:\n";
        M->print(outs(), nullptr);
        std::cout << "\n";
      }
    } else {
      std::cerr << "Parse error: " << parser.error() << "\n";
    }
  }

  // Demo 2: Cubical inline expression
  {
    std::cout << "--- Demo 2: Cubical inline expression ---\n";
    std::string src = 
      "let cubical five = \"data Nat = | zero : Nat | suc : Nat -> Nat\n"
      "def main : Nat = suc (suc (suc (suc (suc zero))))\"\n"
      "print five\n";
    LLVMContext Context;
    std::unique_ptr<Module> M = std::make_unique<Module>("hokkaido_demo2", Context);
    IRBuilder<> Builder(Context);

    Lexer lexer(src);
    Parser parser(lexer);
    auto decls = parser.parse_program();

    if (parser.ok()) {
      CodeGen cg(Context, *M, Builder);
      if (cg.generate(decls)) {
        std::cout << "\nLLVM IR:\n";
        M->print(outs(), nullptr);
        std::cout << "\n";
      }
    } else {
      std::cerr << "Parse error: " << parser.error() << "\n";
    }
  }

  // Demo 3: Cubical with recursive computation
  {
    std::cout << "--- Demo 3: Cubical recursive computation ---\n";
    std::string src =
      "let cubical fact6 = \"data Nat = | zero : Nat | suc : Nat -> Nat\n"
      "def plus : Nat -> Nat -> Nat = \\\\m n. elim (\\\\_. Nat) {\n"
      "  | zero => n\n"
      "  | suc m' => suc (plus m' n)\n"
      "} m\n"
      "def mul : Nat -> Nat -> Nat = \\\\m n. elim (\\\\_. Nat) {\n"
      "  | zero => zero\n"
      "  | suc m' => plus n (mul m' n)\n"
      "} m\n"
      "def fact : Nat -> Nat = \\\\n. elim (\\\\_. Nat) {\n"
      "  | zero => suc zero\n"
      "  | suc n' => mul (suc n') (fact n')\n"
      "} n\n"
      "def main : Nat = fact (suc (suc (suc zero)))\"\n"
      "print fact6\n";
    LLVMContext Context;
    std::unique_ptr<Module> M = std::make_unique<Module>("hokkaido_demo3", Context);
    IRBuilder<> Builder(Context);

    Lexer lexer(src);
    Parser parser(lexer);
    auto decls = parser.parse_program();

    if (parser.ok()) {
      CodeGen cg(Context, *M, Builder);
      if (cg.generate(decls)) {
        std::cout << "\nLLVM IR:\n";
        M->print(outs(), nullptr);
        std::cout << "\n";
      }
    } else {
      std::cerr << "Parse error: " << parser.error() << "\n";
    }
  }

  std::cout << "=== Demo Complete ===\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage();
    run_demo();
    return 0;
  }

  std::filesystem::path filePath(argv[1]);
  std::ifstream ifs(filePath);
  if (!ifs) {
    std::cerr << "Error: cannot open file '" << argv[1] << "'\n";
    return 1;
  }
  std::string Content((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());

  // Initialize LLVM targets
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  std::string TargetTripleStr = sys::getDefaultTargetTriple();
  Triple TargetTriple(TargetTripleStr);
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TargetTripleStr, Error);
  if (!TheTarget) {
    errs() << "Error: " << Error;
    return 1;
  }

  std::string CPU = "generic";
  TargetOptions opt;
  auto RM = std::optional<Reloc::Model>();
  std::unique_ptr<TargetMachine> TM(
      TheTarget->createTargetMachine(TargetTriple, CPU, "", opt, RM));

  LLVMContext Context;
  std::unique_ptr<Module> M = std::make_unique<Module>("hokkaido", Context);
  M->setDataLayout(TM->createDataLayout());
  M->setTargetTriple(TargetTriple);

  IRBuilder<> Builder(Context);

  // Handle .cub files (direct cubical evaluation)
  if (filePath.extension() == ".cub" || filePath.extension() == ".uwuc") {
    cubical_value cv(Content);
    if (cv.valid()) {
      std::cout << cv.str() << "\n";
    } else {
      std::cerr << "Error: cubical evaluation failed\n";
      return 1;
    }
    return 0;
  }

  // Handle .hk files (hokkaido language)
  if (filePath.extension() == ".hk") {
    Lexer lexer(Content);
    Parser parser(lexer);
    auto decls = parser.parse_program();

    if (!parser.ok()) {
      std::cerr << "Parse error: " << parser.error() << "\n";
      return 1;
    }

    CodeGen cg(Context, *M, Builder);
    if (!cg.generate(decls)) {
      return 1;
    }

    M->print(outs(), nullptr);
    return 0;
  }

  std::cerr << "Unsupported file type: " << filePath.extension() << "\n";
  std::cerr << "Supported: .hk (hokkaido), .cub\n";
  return 1;
}