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
// The cubical type triggers compile-time evaluation of the cubical
// expression. The result is embedded as an LLVM constant.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

#include "codegen.h"
#include "cubical.h"
#include "lexer.h"
#include "parser.h"

using namespace llvm;

// =========================================================================
// Main entry point
// =========================================================================

void print_usage() {
  std::cout << "hokkaido — LLVM-based compiler with cubical compile-time evaluation\n\n";
  std::cout << "Usage:\n";
  std::cout << "  hokkaido input.hk              Print LLVM IR to stdout\n";
  std::cout << "  hokkaido input.hk -o output    Compile to executable\n";
  std::cout << "  hokkaido input.cub              Evaluate a .cub file\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage();
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

  // Parse optional -o flag
  std::string OutputPath;
  for (int i = 2; i < argc; i++) {
    if (std::string(argv[i]) == "-o" && i + 1 < argc) {
      OutputPath = argv[++i];
    }
  }

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

    if (OutputPath.empty()) {
      M->print(outs(), nullptr);
    } else {
      std::string ObjPath = OutputPath + ".o";
      std::error_code EC;
      raw_fd_ostream Dest(ObjPath, EC, sys::fs::OF_None);
      if (EC) {
        errs() << "Error: cannot open '" << ObjPath << "': " << EC.message() << "\n";
        return 1;
      }

      LoopAnalysisManager LAM;
      FunctionAnalysisManager FAM;
      CGSCCAnalysisManager CGAM;
      ModuleAnalysisManager MAM;

      PassBuilder PB(TM.get());
      PB.registerModuleAnalyses(MAM);
      PB.registerCGSCCAnalyses(CGAM);
      PB.registerFunctionAnalyses(FAM);
      PB.registerLoopAnalyses(LAM);
      PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

      // TargetMachine::addPassesToEmitFile still requires the legacy PM
      // for the codegen backend step. The new PM infrastructure above is
      // ready for optimization passes as the compiler grows.
      legacy::PassManager LPM;
      TM->registerPassBuilderCallbacks(PB);
      if (TM->addPassesToEmitFile(LPM, Dest, nullptr, CodeGenFileType::ObjectFile)) {
        errs() << "Error: target does not support object emission\n";
        return 1;
      }
      LPM.run(*M);
      Dest.close();

      std::string LinkCmd = "clang " + ObjPath + " -o " + OutputPath;
      int Ret = std::system(LinkCmd.c_str());
      std::remove(ObjPath.c_str());
      if (Ret != 0) {
        std::cerr << "Error: linking failed (is clang installed?)\n";
        return 1;
      }
    }
    return 0;
  }

  std::cerr << "Unsupported file type: " << filePath.extension() << "\n";
  std::cerr << "Supported: .hk (hokkaido), .cub\n";
  return 1;
}