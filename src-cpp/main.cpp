// hokkaido — LLVM-based compiler with cubical compile-time evaluation
// The cubical type triggers compile-time evaluation of the cubical
// expression. The result is embedded as an LLVM constant.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

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
// Minimal C runtime discovery for linking with ld.lld directly
// =========================================================================
//
// ld.lld is purely a linker — unlike clang, it has no built-in knowledge of
// where a given system keeps its C runtime startup objects (crt1.o,
// crti.o, crtn.o), libc, or the dynamic loader path. A compiler driver
// like clang normally works that out for you from the target triple and
// sysroot. Here we do a small best-effort search across common distro
// layouts instead, so the toolchain doesn't need a full clang install just
// to perform the final link step. Anything nonstandard can be overridden
// via HOKKAIDO_CRT_DIR / HOKKAIDO_DYNAMIC_LINKER.

static std::string find_crt_dir() {
  if (const char *env = std::getenv("HOKKAIDO_CRT_DIR")) return env;
  static const char *candidates[] = {
      "/usr/lib/x86_64-linux-gnu",   // Debian/Ubuntu x86_64
      "/usr/lib/aarch64-linux-gnu",  // Debian/Ubuntu arm64
      "/usr/lib64",                  // Fedora/RHEL/openSUSE
      "/usr/lib",                    // Arch and others
      "/lib64",
      "/lib",
  };
  for (const char *dir : candidates) {
    if (std::filesystem::exists(std::string(dir) + "/crt1.o")) return dir;
  }
  return "";
}

static bool find_ld_lld() {
  if (const char *env = std::getenv("HOKKAIDO_LD_LLD")) {
    return std::filesystem::exists(env);
  }
  if (const char *path = std::getenv("PATH")) {
    std::string PathStr(path);
    size_t start = 0;
    while (start <= PathStr.size()) {
      size_t end = PathStr.find(':', start);
      if (end == std::string::npos) end = PathStr.size();
      std::string dir = PathStr.substr(start, end - start);
      if (!dir.empty() && std::filesystem::exists(dir + "/ld.lld")) {
        return true;
      }
      start = end + 1;
    }
  }
  return false;
}

static std::string find_dynamic_linker() {
  if (const char *env = std::getenv("HOKKAIDO_DYNAMIC_LINKER")) return env;
  static const char *candidates[] = {
      "/lib64/ld-linux-x86-64.so.2",   // glibc x86_64
      "/lib/ld-linux-aarch64.so.1",    // glibc arm64
      "/lib/ld-linux.so.2",            // glibc i386
      "/lib/ld-musl-x86_64.so.1",      // musl x86_64
  };
  for (const char *path : candidates) {
    if (std::filesystem::exists(path)) return path;
  }
  return "";
}

// =========================================================================
// Main entry point
// =========================================================================

void print_usage() {
  std::cout << "hokkaido — LLVM-based compiler with cubical compile-time evaluation\n\n";
  std::cout << "Usage:\n";
  std::cout << "  hokkaido input.hk              Print LLVM IR to stdout\n";
  std::cout << "  hokkaido input.hk -o output    Compile to an object file (output.o)\n";
  std::cout << "  hokkaido input.cub              Evaluate a .cub file\n\n";
  std::cout << "hokkaido does not link executables itself — it only emits an object\n";
  std::cout << "file. After compiling, link it yourself with 'ld.lld', 'clang', or your\n";
  std::cout << "platform's usual linker. Run with -o to see a suggested link command for\n";
  std::cout << "this object file once it's been emitted.\n";
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

  // Parse optional -o flag and any extra linker flags (e.g. -lm, -lcurl,
  // -L/path) for linking against C libraries used by `extern fn` declarations.
  std::string OutputPath;
  std::vector<std::string> ExtraLinkArgs;
  for (int i = 2; i < argc; i++) {
    std::string Arg = argv[i];
    if (Arg == "-o" && i + 1 < argc) {
      OutputPath = argv[++i];
    } else {
      ExtraLinkArgs.push_back(Arg);
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
    auto included_files = std::make_shared<std::set<std::string>>();
    {
      std::error_code ec;
      auto canonical = std::filesystem::weakly_canonical(filePath, ec);
      included_files->insert((ec ? filePath : canonical).string());
    }
    Parser parser(lexer, filePath.parent_path().string(), included_files);
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

      std::string CrtDir = find_crt_dir();
      std::string DynamicLinker = find_dynamic_linker();
      bool HaveLdLld = find_ld_lld();

      std::cout << "Object file written to: " << ObjPath << "\n\n";
      std::cout << "hokkaido does not link executables itself. To produce '"
                << OutputPath << "', link the object file yourself, e.g.:\n\n";

      if (HaveLdLld && !CrtDir.empty() && !DynamicLinker.empty()) {
        // Equivalent to what `clang ObjPath -o OutputPath` does under the
        // hood, minus the parts of clang we don't need (no C compilation
        // happens here — the .o file above was already produced by LLVM).
        // Order matters to the linker: crt1.o, crti.o, then user objects
        // and libraries, then crtn.o.
        std::cout << "  ld.lld -o " << OutputPath
                   << " -dynamic-linker " << DynamicLinker
                   << " " << CrtDir << "/crt1.o"
                   << " " << CrtDir << "/crti.o"
                   << " " << ObjPath
                   << " -L" << CrtDir << " -lc";
        for (auto &a : ExtraLinkArgs) std::cout << " " << a;
        std::cout << " " << CrtDir << "/crtn.o\n";
      } else {
        // Couldn't find everything needed for a precise ld.lld invocation;
        // clang is a much simpler one-liner that figures all of that out
        // itself, so suggest that instead.
        std::cout << "  clang " << ObjPath << " -o " << OutputPath;
        for (auto &a : ExtraLinkArgs) std::cout << " " << a;
        std::cout << "\n";
        if (!HaveLdLld) {
          std::cout << "\n  ('ld.lld' was not found on PATH.)\n";
        }
        if (CrtDir.empty() || DynamicLinker.empty()) {
          std::cout << "  (Could not locate C runtime startup objects or "
                       "the dynamic linker for a direct ld.lld invocation; "
                       "install glibc development files, e.g. 'libc6-dev' "
                       "on Debian/Ubuntu or 'glibc-devel' on Fedora/RHEL, "
                       "or set HOKKAIDO_CRT_DIR and "
                       "HOKKAIDO_DYNAMIC_LINKER.)\n";
        }
      }
    }
    return 0;
  }

  std::cerr << "Unsupported file type: " << filePath.extension() << "\n";
  std::cerr << "Supported: .hk (hokkaido), .cub\n";
  return 1;
}