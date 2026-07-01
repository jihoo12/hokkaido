#include "codegen.h"

#include <fstream>
#include <iostream>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include "cubical.h"

using namespace llvm;

// -------------------------------------------------------------------------
// LLVM type mapping
// -------------------------------------------------------------------------

Type *CodeGen::get_llvm_type(TypeKind kind) {
  switch (kind) {
    case TypeKind::Void:    return Type::getVoidTy(Context);
    case TypeKind::Int8:    return Type::getInt8Ty(Context);
    case TypeKind::Int32:   return Type::getInt32Ty(Context);
    case TypeKind::Int64:   return Type::getInt64Ty(Context);
    case TypeKind::Float16: return Type::getHalfTy(Context);
    case TypeKind::Float32: return Type::getFloatTy(Context);
    case TypeKind::Float64: return Type::getDoubleTy(Context);
    case TypeKind::Bool:    return Type::getInt1Ty(Context);
    case TypeKind::String:  return PointerType::getUnqual(Context);
    case TypeKind::Cubical: return Type::getInt64Ty(Context);
    case TypeKind::Struct:  return nullptr; // must use annotation overload
  }
  return nullptr;
}

Type *CodeGen::get_llvm_type(const TypeAnnotation &ann) {
  Type *base = nullptr;
  if (ann.kind == TypeKind::Struct) {
    auto it = struct_types.find(ann.struct_name);
    if (it != struct_types.end()) {
      base = it->second;
      for (int i = 0; i < ann.pointer_depth; i++)
        base = PointerType::getUnqual(Context);
      return base;
    }
    // Fallback: check enum types
    auto eit = enum_types.find(ann.struct_name);
    if (eit != enum_types.end()) {
      base = eit->second;
      for (int i = 0; i < ann.pointer_depth; i++)
        base = PointerType::getUnqual(Context);
      return base;
    }
    errs() << "Error: unknown struct/enum type '" << ann.struct_name << "'\n";
    return nullptr;
  }
  if (ann.kind == TypeKind::Enum) {
    auto it = enum_types.find(ann.struct_name);
    if (it == enum_types.end()) {
      errs() << "Error: unknown enum type '" << ann.struct_name << "'\n";
      return nullptr;
    }
    base = it->second;
    for (int i = 0; i < ann.pointer_depth; i++)
      base = PointerType::getUnqual(Context);
    return base;
  }
  base = get_llvm_type(ann.kind);
  // If it's an array, wrap in ArrayType
  if (ann.array_size > 0) {
    return ArrayType::get(base, ann.array_size);
  }
  for (int i = 0; i < ann.pointer_depth; i++)
    base = PointerType::getUnqual(Context);
  return base;
}

// -------------------------------------------------------------------------
// Struct helpers
// -------------------------------------------------------------------------

void CodeGen::register_struct_decl(StructDecl *decl) {
  // Create or get the opaque struct type
  StructType *st = StructType::create(Context, decl->name);

  std::vector<Type *> member_types;
  std::vector<std::pair<std::string, TypeAnnotation>> fields_info;

  for (auto &field : decl->fields) {
    Type *field_type = get_llvm_type(field.type_ann);
    if (!field_type) {
      errs() << "Error: invalid field type in struct '" << decl->name << "'\n";
      return;
    }
    member_types.push_back(field_type);
    fields_info.push_back({field.name, field.type_ann});
  }

  st->setBody(member_types);
  struct_types[decl->name] = st;
  struct_fields[decl->name] = fields_info;
}

StructType *CodeGen::get_struct_type(const std::string &name) {
  auto it = struct_types.find(name);
  if (it == struct_types.end()) return nullptr;
  return it->second;
}

int CodeGen::get_struct_field_index(const std::string &struct_name,
                                     const std::string &field_name) {
  auto it = struct_fields.find(struct_name);
  if (it == struct_fields.end()) return -1;
  for (size_t i = 0; i < it->second.size(); i++) {
    if (it->second[i].first == field_name)
      return (int)i;
  }
  return -1;
}

TypeAnnotation CodeGen::resolve_expr_type(Expr *expr) {
  if (auto *id = dynamic_cast<IdentExpr *>(expr)) {
    auto it = named_type_anns.find(id->name);
    if (it != named_type_anns.end())
      return it->second;
    // Fallback: check named_types
    auto kt = named_types.find(id->name);
    if (kt != named_types.end())
      return {kt->second};
    return {TypeKind::Void};
  }
  if (auto *field = dynamic_cast<FieldAccessExpr *>(expr)) {
    TypeAnnotation base = resolve_expr_type(field->object.get());
    if (base.kind == TypeKind::Void || base.kind == TypeKind::Struct)
      return get_struct_field_type(base.struct_name, field->field);
    return {TypeKind::Void};
  }
  if (auto *deref = dynamic_cast<DerefExpr *>(expr)) {
    TypeAnnotation base = resolve_expr_type(deref->operand.get());
    if (base.pointer_depth > 0) {
      base.pointer_depth--;
      return base;
    }
    return {TypeKind::Void};
  }
  if (auto *sub = dynamic_cast<SubscriptExpr *>(expr)) {
    TypeAnnotation base = resolve_expr_type(sub->array.get());
    if (base.array_size > 0) {
      base.array_size = 0;
      return base;
    }
    if (base.pointer_depth > 0) {
      // Pointer subscript: int* ptr; ptr[i] → int
      base.pointer_depth--;
      return base;
    }
    return {base.kind};
  }
  if (auto *bin = dynamic_cast<BinaryExpr *>(expr)) {
    // For pointer arithmetic, return the pointer type
    TypeAnnotation l = resolve_expr_type(bin->left.get());
    TypeAnnotation r = resolve_expr_type(bin->right.get());
    if (l.pointer_depth > 0) return l;
    if (r.pointer_depth > 0) return r;
    // Comparison and logical operators return bool
    switch (bin->op) {
      case BinOp::Eq: case BinOp::Ne:
      case BinOp::Less: case BinOp::Greater: case BinOp::Le: case BinOp::Ge:
      case BinOp::And: case BinOp::Or:
        return {TypeKind::Bool};
      default:
        return {TypeKind::Int64};
    }
  }
  if (dynamic_cast<NumberExpr *>(expr))
    return {TypeKind::Int64};
  if (auto *ctor = dynamic_cast<ConstructorExpr *>(expr)) {
    // Find which enum this variant belongs to
    for (auto &[ename, _] : enum_types) {
      int vi = get_enum_variant_index(ename, ctor->variant_name);
      if (vi >= 0)
        return {TypeKind::Enum, 0, 0, ename};
    }
    // Fallback: check if it's a struct constructor
    if (struct_types.count(ctor->variant_name) > 0)
      return {TypeKind::Struct, 0, 0, ctor->variant_name};
    return {TypeKind::Void};
  }
  return {TypeKind::Void};
}

Value *CodeGen::get_lvalue_ptr(Expr *expr, Type **out_type) {
  if (auto *id = dynamic_cast<IdentExpr *>(expr)) {
    auto it = named_values.find(id->name);
    if (it == named_values.end()) {
      errs() << "Error: undefined variable '" << id->name << "'\n";
      return nullptr;
    }
    if (out_type) *out_type = it->second->getAllocatedType();
    return it->second;
  }
  if (auto *deref = dynamic_cast<DerefExpr *>(expr)) {
    Value *ptr = eval_expr(deref->operand.get(), PointerType::getUnqual(Context));
    if (!ptr) return nullptr;
    if (out_type) {
      TypeAnnotation ann = resolve_expr_type(deref);
      *out_type = get_llvm_type(ann);
    }
    return ptr;
  }
  if (auto *sub = dynamic_cast<SubscriptExpr *>(expr)) {
    TypeAnnotation base_ann = resolve_expr_type(sub->array.get());
    Value *index = eval_expr(sub->index.get(), Type::getInt64Ty(Context));
    if (!index) return nullptr;

    if (base_ann.array_size > 0) {
      // Array-based subscript: get alloca via lvalue, GEP with [0, i]
      Type *arr_llvm_type = nullptr;
      Value *arr_ptr = get_lvalue_ptr(sub->array.get(), &arr_llvm_type);
      if (!arr_ptr) return nullptr;
      Value *indices[] = {
        ConstantInt::get(Type::getInt64Ty(Context), 0),
        index
      };
      Value *gep = Builder.CreateGEP(arr_llvm_type, arr_ptr, indices, "elem_ptr");
      if (out_type) {
        if (auto *arr = dyn_cast_or_null<ArrayType>(arr_llvm_type))
          *out_type = arr->getElementType();
      }
      return gep;
    }

    // Pointer-based subscript: evaluate base to get pointer, single GEP
    if (base_ann.pointer_depth > 0) {
      Type *base_llvm = get_llvm_type(base_ann);
      if (!base_llvm) return nullptr;
      Value *arr_ptr = eval_expr(sub->array.get(), base_llvm);
      if (!arr_ptr) return nullptr;
      base_ann.pointer_depth--;
      Type *pointee = get_llvm_type(base_ann);
      if (!pointee) pointee = Type::getInt8Ty(Context);
      Value *gep = Builder.CreateGEP(pointee, arr_ptr, index, "elem_ptr");
      if (out_type) *out_type = pointee;
      return gep;
    }

    errs() << "Error: subscript requires an array or pointer\n";
    return nullptr;
  }
  if (auto *field = dynamic_cast<FieldAccessExpr *>(expr)) {
    Type *struct_type = nullptr;
    Value *struct_ptr = get_lvalue_ptr(field->object.get(), &struct_type);
    if (!struct_ptr) return nullptr;

    StructType *st = dyn_cast<StructType>(struct_type);
    if (!st) {
      errs() << "Error: field access on non-struct type\n";
      return nullptr;
    }

    std::string struct_name = st->getName().str();
    int field_idx = get_struct_field_index(struct_name, field->field);
    if (field_idx < 0) {
      errs() << "Error: struct '" << struct_name << "' has no field named '"
             << field->field << "'\n";
      return nullptr;
    }

    Value *field_ptr = Builder.CreateStructGEP(
        struct_type, struct_ptr, field_idx, field->field);
    if (out_type) {
      TypeAnnotation field_ann = get_struct_field_type(struct_name, field->field);
      *out_type = get_llvm_type(field_ann);
    }
    return field_ptr;
  }
  return nullptr;
}

TypeAnnotation CodeGen::get_struct_field_type(const std::string &struct_name,
                                               const std::string &field_name) {
  auto it = struct_fields.find(struct_name);
  if (it == struct_fields.end()) return {TypeKind::Void};
  for (auto &f : it->second) {
    if (f.first == field_name)
      return f.second;
  }
  return {TypeKind::Void};
}

// -------------------------------------------------------------------------
// Enum helpers
// -------------------------------------------------------------------------

void CodeGen::register_enum_decl(AdtDecl *decl) {
  SmallVector<Type *, 8> member_types;
  member_types.push_back(Type::getInt64Ty(Context));
  for (auto &var : decl->variants) {
    SmallVector<Type *, 4> field_types;
    for (auto &f : var.fields) {
      Type *ft = get_llvm_type(f.type_ann);
      if (!ft) ft = Type::getInt64Ty(Context);
      field_types.push_back(ft);
    }
    StructType *var_st = StructType::create(Context, field_types,
                                            decl->name + "::" + var.name);
    member_types.push_back(var_st);
  }
  StructType *st = StructType::create(Context, member_types, decl->name);
  enum_types[decl->name] = st;

  // Store variant field info in struct_fields for lookup
  std::vector<std::pair<std::string, std::vector<StructField>>> var_list;
  for (auto &var : decl->variants) {
    std::vector<std::pair<std::string, TypeAnnotation>> fields_info;
    std::vector<StructField> sf_list;
    for (auto &f : var.fields) {
      fields_info.push_back({f.name, f.type_ann});
      sf_list.push_back({f.name, f.type_ann});
    }
    struct_fields[decl->name + "::" + var.name] = fields_info;
    var_list.push_back({var.name, sf_list});
  }
  enum_variants[decl->name] = var_list;
}

int CodeGen::get_enum_variant_index(const std::string &enum_name,
                                     const std::string &variant_name) {
  auto it = enum_variants.find(enum_name);
  if (it == enum_variants.end()) return -1;
  for (size_t i = 0; i < it->second.size(); i++) {
    if (it->second[i].first == variant_name) return (int)i;
  }
  return -1;
}

// -------------------------------------------------------------------------
// Top-level generate
// -------------------------------------------------------------------------

bool CodeGen::generate(const std::vector<std::unique_ptr<Decl>> &decls) {
  // Pass 0: register all struct and enum types first
  for (auto &decl : decls) {
    if (auto *sd = dynamic_cast<StructDecl *>(decl.get())) {
      register_struct_decl(sd);
    } else if (auto *ed = dynamic_cast<AdtDecl *>(decl.get())) {
      register_enum_decl(ed);
    }
  }

  // Pass 1: create all function declarations so they can be referenced
  std::vector<FnDecl *> fn_decls;
  FnDecl *user_main = nullptr;
  for (auto &decl : decls) {
    if (auto *fn = dynamic_cast<FnDecl *>(decl.get())) {
      if (freestanding && fn->is_extern) {
        errs() << "Error: 'extern fn " << fn->name << "' is not allowed in "
                  "freestanding mode (no libc is linked, so there is no "
                  "symbol for it to resolve against)\n";
        return false;
      }

      std::vector<Type *> param_types;
      for (auto &p : fn->params)
        param_types.push_back(get_llvm_type(p.type_ann));

      FunctionType *FT = FunctionType::get(
          get_llvm_type(fn->return_type), param_types, fn->is_variadic);
      // Rename user "main" so it doesn't conflict with the auto-generated
      // entry point. An `extern fn main(...)` is a foreign symbol, not the
      // program's entry point, so it's left alone and not treated specially.
      std::string llvm_name = fn->name;
      if (!fn->is_extern && fn->name == "main") {
        llvm_name = "__user_main";
        user_main = fn;
      }
      Function::Create(FT, Function::ExternalLinkage, llvm_name, &M);
      fn_decls.push_back(fn);
    }
  }

  if (!user_main) {
    errs() << "Error: no main function defined (add 'fn main() -> int { ... }')\n";
    return false;
  }

  if (user_main->return_type.kind != TypeKind::Int64) {
    errs() << "Error: main function must return int64\n";
    return false;
  }

  if (user_main->params.size() != 0 && user_main->params.size() != 2) {
    errs() << "Error: main function must have 0 or 2 parameters"
              " (argc: int, argv: int8**)\n";
    return false;
  }
  if (user_main->params.size() == 2) {
    // argc: int (Int64), argv: int8** (Int8, pointer_depth=2)
    auto &p0 = user_main->params[0];
    auto &p1 = user_main->params[1];
    if (p0.type_ann.kind != TypeKind::Int64 ||
        p1.type_ann.kind != TypeKind::Int8 ||
        p1.type_ann.pointer_depth != 2) {
      errs() << "Error: main parameters must be (argc: int, argv: int8**)\n";
      return false;
    }
  }

  // Pass 2: generate function bodies (extern declarations have none —
  // they're foreign symbols resolved at link time)
  for (auto *fn : fn_decls) {
    if (fn->is_extern) continue;
    std::string llvm_name = (fn->name == "main") ? "__user_main" : fn->name;
    if (!gen_fn_body(fn, M.getFunction(llvm_name)))
      return false;
  }

  // Pass 3: main entry point — wraps user main and handles top-level lets
  if (!gen_main_body(decls))
    return false;

  if (verifyModule(M, &errs())) {
    errs() << "Error: module verification failed\n";
    return false;
  }
  return true;
}

bool CodeGen::gen_main_body(const std::vector<std::unique_ptr<Decl>> &decls) {
  Function *user_main = M.getFunction("__user_main");
  bool needs_args = (user_main && user_main->arg_size() == 2 && !freestanding);

  // Build the C main signature:
  //   int main(int argc, char *argv[])
  // The C runtime on Linux x86-64 always pushes argc / argv, so the wrapper
  // always declares them; we just don't forward them when the user's main
  // doesn't need them.
  // In freestanding mode, main is a raw ELF entry point (no CRT), so it
  // doesn't receive argc/argv as normal function parameters.
  std::vector<Type *> main_param_types;
  if (freestanding) {
    main_param_types = {};
  } else {
    main_param_types = {Type::getInt32Ty(Context),
                        PointerType::getUnqual(Context)};
  }
  FunctionType *FT = FunctionType::get(Type::getInt32Ty(Context), main_param_types, false);
  MainFn = Function::Create(FT, Function::ExternalLinkage, "main", &M);
  EntryBB = BasicBlock::Create(Context, "entry", MainFn);
  Builder.SetInsertPoint(EntryBB);
  named_values.clear();
  named_types.clear();

  for (auto &decl : decls) {
    if (auto *let = dynamic_cast<LetDecl *>(decl.get())) {
      if (!gen_let_decl(let)) return false;
    }
  }

  // Call user main and return its value truncated to i32
  Value *result;
  if (user_main) {
    if (needs_args) {
      Function::arg_iterator ai = MainFn->arg_begin();
      Value *argc_i32 = ai;     // i32
      Value *argv = ++ai;       // ptr (i8**)
      argc_i32->setName("argc");
      argv->setName("argv");
      Value *argc_i64 = Builder.CreateSExt(argc_i32, Type::getInt64Ty(Context), "argc.ext");
      result = Builder.CreateCall(user_main, {argc_i64, argv});
    } else {
      result = Builder.CreateCall(user_main, {});
    }
  } else {
    result = ConstantInt::get(Type::getInt64Ty(Context), 0);
  }

  if (freestanding) {
    FunctionType *AsmFT =
        FunctionType::get(Type::getVoidTy(Context), {Type::getInt64Ty(Context)}, false);
    InlineAsm *ExitSyscall = InlineAsm::get(
        AsmFT, "movq $$60, %rax\n\tsyscall",
        "{rdi},~{rax},~{rcx},~{r11},~{memory}",
        /*hasSideEffects=*/true);
    Builder.CreateCall(ExitSyscall, {result});
    Builder.CreateUnreachable();
  } else {
    Value *truncated = Builder.CreateTrunc(result, Type::getInt32Ty(Context));
    Builder.CreateRet(truncated);
  }
  return true;
}

// -------------------------------------------------------------------------
// Expression evaluation
// -------------------------------------------------------------------------

Value *CodeGen::eval_expr(Expr *expr, Type *expected_type) {
  if (auto *num = dynamic_cast<NumberExpr *>(expr)) {
    if (expected_type && expected_type->isFPOrFPVectorTy())
      return ConstantFP::get(expected_type, num->value);
    Type *t = expected_type ? expected_type : Type::getInt64Ty(Context);
    return ConstantInt::get(t, (int64_t)num->value);
  }

  if (auto *str = dynamic_cast<StringExpr *>(expr)) {
    return Builder.CreateGlobalString(str->value, "str");
  }

  if (auto *id = dynamic_cast<IdentExpr *>(expr)) {
    auto it = named_values.find(id->name);
    if (it == named_values.end()) {
      errs() << "Error: undefined variable '" << id->name << "'\n";
      return nullptr;
    }
    // Array-to-pointer decay: if expected_type is a pointer and the
    // variable is an array, return a pointer to its first element.
    Type *alloc_type = it->second->getAllocatedType();
    if (auto *arr_type = dyn_cast<ArrayType>(alloc_type)) {
      if (expected_type && expected_type->isPointerTy()) {
        Value *indices[] = {
          ConstantInt::get(Type::getInt64Ty(Context), 0),
          ConstantInt::get(Type::getInt64Ty(Context), 0)
        };
        return Builder.CreateGEP(arr_type, it->second, indices, id->name);
      }
    }
    if (expected_type)
      return Builder.CreateLoad(expected_type, it->second, id->name);
    return Builder.CreateLoad(alloc_type, it->second, id->name);
  }

  if (auto *null_expr = dynamic_cast<NullExpr *>(expr)) {
    return ConstantPointerNull::get(cast<PointerType>(expected_type));
  }

  // Constructor expression: VariantName { field: expr, ... }
  if (auto *ctor = dynamic_cast<ConstructorExpr *>(expr)) {
    // Check if this is an enum variant constructor
    StructType *enum_st = nullptr;
    std::string enum_name;
    int variant_idx = -1;
    for (auto &[ename, st] : enum_types) {
      int idx = get_enum_variant_index(ename, ctor->variant_name);
      if (idx >= 0) {
        enum_st = st;
        enum_name = ename;
        variant_idx = idx;
        break;
      }
    }
    if (enum_st) {
      // Enum variant constructor
      Value *result = UndefValue::get(enum_st);
      Type *tag_type = enum_st->getElementType(0);
      result = Builder.CreateInsertValue(result,
          ConstantInt::get(tag_type, variant_idx), {0});
      StructType *var_type = cast<StructType>(enum_st->getElementType(1 + variant_idx));
      Value *var_data = UndefValue::get(var_type);
      for (size_t fi = 0; fi < ctor->fields.size(); fi++) {
        auto &[field_name, field_expr] = ctor->fields[fi];
        TypeAnnotation field_ann = get_struct_field_type(
            enum_name + "::" + ctor->variant_name, field_name);
        Type *field_type = get_llvm_type(field_ann);
        if (!field_type) field_type = Type::getInt64Ty(Context);
        int actual_idx = get_struct_field_index(
            enum_name + "::" + ctor->variant_name, field_name);
        if (actual_idx < 0) {
          errs() << "Error: variant '" << ctor->variant_name
                 << "' has no field '" << field_name << "'\n";
          return nullptr;
        }
        Value *fv = eval_expr(field_expr.get(), field_type);
        if (!fv) return nullptr;
        var_data = Builder.CreateInsertValue(var_data, fv,
            {(unsigned)actual_idx});
      }
      result = Builder.CreateInsertValue(result, var_data, {1 + (unsigned)variant_idx});
      return result;
    }

    // Fallback: try as a struct constructor
    auto st_it = struct_types.find(ctor->variant_name);
    if (st_it != struct_types.end()) {
      StructType *st = st_it->second;
      Value *result = UndefValue::get(st);
      for (size_t fi = 0; fi < ctor->fields.size(); fi++) {
        auto &[field_name, field_expr] = ctor->fields[fi];
        TypeAnnotation field_ann = get_struct_field_type(
            ctor->variant_name, field_name);
        Type *field_type = get_llvm_type(field_ann);
        if (!field_type) field_type = Type::getInt64Ty(Context);
        int actual_idx = get_struct_field_index(
            ctor->variant_name, field_name);
        if (actual_idx < 0) {
          errs() << "Error: struct '" << ctor->variant_name
                 << "' has no field '" << field_name << "'\n";
          return nullptr;
        }
        Value *fv = eval_expr(field_expr.get(), field_type);
        if (!fv) return nullptr;
        result = Builder.CreateInsertValue(result, fv,
            {(unsigned)actual_idx});
      }
      return result;
    }

    errs() << "Error: unknown variant '" << ctor->variant_name
           << "' in constructor expression\n";
    return nullptr;
  }

  if (auto *addr = dynamic_cast<AddressOfExpr *>(expr)) {
    Type *ptr_type = nullptr;
    Value *lvalue_ptr = get_lvalue_ptr(addr->operand.get(), &ptr_type);
    if (!lvalue_ptr) {
      errs() << "Error: address-of requires an lvalue expression\n";
      return nullptr;
    }
    if (!ptr_type)
      ptr_type = lvalue_ptr->getType();
    return lvalue_ptr;
  }

  if (auto *deref = dynamic_cast<DerefExpr *>(expr)) {
    Value *ptr = nullptr;
    if (auto *id = dynamic_cast<IdentExpr *>(deref->operand.get())) {
      auto it = named_values.find(id->name);
      if (it == named_values.end()) {
        errs() << "Error: undefined variable '" << id->name << "'\n";
        return nullptr;
      }
      ptr = Builder.CreateLoad(it->second->getAllocatedType(), it->second, id->name);
    } else {
      ptr = eval_expr(deref->operand.get(), PointerType::getUnqual(Context));
    }
    if (!ptr) return nullptr;
    return Builder.CreateLoad(expected_type, ptr);
  }

  if (auto *sub = dynamic_cast<SubscriptExpr *>(expr)) {
    TypeAnnotation base_ann = resolve_expr_type(sub->array.get());
    bool is_array_sub = base_ann.array_size > 0;
    bool is_ptr_sub = !is_array_sub && base_ann.pointer_depth > 0;
    Value *elem_ptr = nullptr;

    if (is_array_sub) {
      // Array-based subscript: GEP with [0, i]
      Type *arr_llvm_type = nullptr;
      Value *arr_ptr = get_lvalue_ptr(sub->array.get(), &arr_llvm_type);
      if (!arr_ptr) return nullptr;
      Value *index = eval_expr(sub->index.get(), Type::getInt64Ty(Context));
      if (!index) return nullptr;
      Value *indices[] = {
        ConstantInt::get(Type::getInt64Ty(Context), 0),
        index
      };
      elem_ptr = Builder.CreateGEP(arr_llvm_type, arr_ptr, indices, "elem_ptr");
    } else if (is_ptr_sub) {
      // Pointer-based subscript: evaluate base to get pointer, single GEP
      Type *base_llvm = get_llvm_type(base_ann);
      if (!base_llvm) return nullptr;
      Value *arr_ptr = eval_expr(sub->array.get(), base_llvm);
      if (!arr_ptr) return nullptr;
      Value *index = eval_expr(sub->index.get(), Type::getInt64Ty(Context));
      if (!index) return nullptr;
      TypeAnnotation pointee_ann = base_ann;
      pointee_ann.pointer_depth--;
      Type *pointee = get_llvm_type(pointee_ann);
      if (!pointee) pointee = Type::getInt8Ty(Context);
      elem_ptr = Builder.CreateGEP(pointee, arr_ptr, index, "elem_ptr");
    } else {
      errs() << "Error: subscript requires an array or pointer\n";
      return nullptr;
    }
    // Load with the actual element type
    Type *load_type = expected_type;
    if (is_array_sub)
      load_type = get_llvm_type(TypeAnnotation{base_ann.kind, 0, 0, base_ann.struct_name});
    else if (is_ptr_sub) {
      TypeAnnotation elem_ann = base_ann;
      elem_ann.pointer_depth--;
      load_type = get_llvm_type(elem_ann);
    }
    if (!load_type) load_type = expected_type;
    return Builder.CreateLoad(load_type, elem_ptr);
  }

  // Field access: obj.field
  if (auto *field = dynamic_cast<FieldAccessExpr *>(expr)) {
    // Resolve the base expression to get a struct value
    TypeAnnotation base_ann = resolve_expr_type(field->object.get());
    if (base_ann.kind != TypeKind::Struct) {
      errs() << "Error: field access on non-struct expression\n";
      return nullptr;
    }

    // Evaluate the base expression to get the struct value
    Type *base_llvm_type = get_llvm_type(base_ann);
    if (!base_llvm_type) return nullptr;
    Value *obj_val = eval_expr(field->object.get(), base_llvm_type);
    if (!obj_val) return nullptr;

    // Find field index
    int field_idx = get_struct_field_index(base_ann.struct_name, field->field);
    if (field_idx < 0) {
      errs() << "Error: struct '" << base_ann.struct_name << "' has no field named '"
             << field->field << "'\n";
      return nullptr;
    }

    // Get the field's LLVM type directly from the struct type
    Type *obj_type = obj_val->getType();
    StructType *st = dyn_cast<StructType>(obj_type);
    if (!st) {
      errs() << "Error: field access target is not a struct type\n";
      return nullptr;
    }

    return Builder.CreateExtractValue(obj_val, {(unsigned)field_idx}, field->field);
  }

  // Match expression
  if (auto *match = dynamic_cast<MatchExpr *>(expr)) {
    TypeAnnotation val_ann = resolve_expr_type(match->value.get());
    Type *val_type = get_llvm_type(val_ann);
    if (!val_type) val_type = expected_type;

    Value *val = eval_expr(match->value.get(), val_type);
    if (!val) return nullptr;

    auto saved_named_values = named_values;
    Function *fn = Builder.GetInsertBlock()->getParent();

    // Use alloca instead of phi to avoid SSA dominance issues with nested matches
    AllocaInst *result_alloca = Builder.CreateAlloca(expected_type, nullptr, "match_result");

    BasicBlock *merge_bb = BasicBlock::Create(Context, "match_merge", fn);
    BasicBlock *else_bb = BasicBlock::Create(Context, "match_else", fn);
    BasicBlock *current_bb = Builder.GetInsertBlock();

    for (size_t i = 0; i < match->arms.size(); i++) {
      auto &arm = match->arms[i];
      bool is_last = (i == match->arms.size() - 1);

      BasicBlock *body_bb = BasicBlock::Create(Context, "arm_body", fn);
      BasicBlock *next_bb = is_last ? else_bb
                                     : BasicBlock::Create(Context, "arm_check", fn);

      Builder.SetInsertPoint(current_bb);
      Value *cond = gen_pattern_check(arm.pattern.get(), val, val_ann);
      if (!cond) return nullptr;

      Builder.CreateCondBr(cond, body_bb, next_bb);

      Builder.SetInsertPoint(body_bb);
      named_values = saved_named_values;
      if (!gen_pattern_bind(arm.pattern.get(), val, val_ann)) return nullptr;

      Value *arm_val = eval_expr(arm.expr.get(), expected_type);
      if (!arm_val) return nullptr;
      Builder.CreateStore(arm_val, result_alloca);
      Builder.CreateBr(merge_bb);

      current_bb = next_bb;
    }

    // else block: store default zero value
    Builder.SetInsertPoint(else_bb);
    Builder.CreateStore(Constant::getNullValue(expected_type), result_alloca);
    Builder.CreateBr(merge_bb);

    Builder.SetInsertPoint(merge_bb);
    named_values = std::move(saved_named_values);
    return Builder.CreateLoad(expected_type, result_alloca);
  }

  if (auto *arr_lit = dynamic_cast<ArrayLitExpr *>(expr)) {
    // Array literals are only valid when a destination array type is known
    if (auto *arr_type = dyn_cast<ArrayType>(expected_type)) {
      return eval_array_literal(arr_lit, arr_type);
    }
    // Fallback: evaluate as pointer (for pointer-type expected)
    return ConstantPointerNull::get(cast<PointerType>(expected_type));
  }

  if (auto *un = dynamic_cast<UnaryExpr *>(expr)) {
    Value *op = eval_expr(un->operand.get(), expected_type);
    if (!op) return nullptr;
    if (expected_type && expected_type->isFPOrFPVectorTy())
      return Builder.CreateFNeg(op);
    return Builder.CreateNeg(op);
  }

  if (auto *bin = dynamic_cast<BinaryExpr *>(expr)) {
    Value *l = eval_expr(bin->left.get(), expected_type);
    Value *r = eval_expr(bin->right.get(), expected_type);
    if (!l || !r) return nullptr;

    bool is_float = expected_type && expected_type->isFPOrFPVectorTy();

    // Pointer arithmetic: ptr + n, ptr - n, n + ptr
    if ((bin->op == BinOp::Add || bin->op == BinOp::Sub) &&
        (l->getType()->isPointerTy() || r->getType()->isPointerTy())) {
      Type *pointee = nullptr;
      Value *ptr_val = nullptr;
      Value *idx_val = nullptr;
      if (l->getType()->isPointerTy() && !r->getType()->isPointerTy()) {
        ptr_val = l; idx_val = r;
        if (bin->op == BinOp::Sub) idx_val = Builder.CreateNeg(r);
        // Get pointee type from left operand's annotation
        TypeAnnotation ann = resolve_expr_type(bin->left.get());
        if (ann.pointer_depth > 0) {
          ann.pointer_depth--;
          pointee = get_llvm_type(ann);
        }
      } else if (r->getType()->isPointerTy() && !l->getType()->isPointerTy()) {
        ptr_val = r; idx_val = l;
        TypeAnnotation ann = resolve_expr_type(bin->right.get());
        if (ann.pointer_depth > 0) {
          ann.pointer_depth--;
          pointee = get_llvm_type(ann);
        }
      }
      if (pointee && ptr_val && idx_val)
        return Builder.CreateGEP(pointee, ptr_val, idx_val, "ptr.offset");
      // Fallback: if we couldn't determine pointee type, use int8
      if (!pointee && ptr_val && idx_val)
        return Builder.CreateGEP(Type::getInt8Ty(Context), ptr_val, idx_val, "ptr.offset");
    }

    switch (bin->op) {
      case BinOp::Add: return is_float ? Builder.CreateFAdd(l, r) : Builder.CreateAdd(l, r);
      case BinOp::Sub: return is_float ? Builder.CreateFSub(l, r) : Builder.CreateSub(l, r);
      case BinOp::Mul: return is_float ? Builder.CreateFMul(l, r) : Builder.CreateMul(l, r);
      case BinOp::Div: return is_float ? Builder.CreateFDiv(l, r) : Builder.CreateSDiv(l, r);
      case BinOp::Eq: {
        Value *cmp = Builder.CreateICmpEQ(l, r);
        if (expected_type && !expected_type->isIntegerTy(1))
          return Builder.CreateZExt(cmp, expected_type);
        return cmp;
      }
      case BinOp::Ne: {
        Value *cmp = Builder.CreateICmpNE(l, r);
        if (expected_type && !expected_type->isIntegerTy(1))
          return Builder.CreateZExt(cmp, expected_type);
        return cmp;
      }
      case BinOp::Less: {
        Value *cmp = Builder.CreateICmpSLT(l, r);
        if (expected_type && !expected_type->isIntegerTy(1))
          return Builder.CreateZExt(cmp, expected_type);
        return cmp;
      }
      case BinOp::Greater: {
        Value *cmp = Builder.CreateICmpSGT(l, r);
        if (expected_type && !expected_type->isIntegerTy(1))
          return Builder.CreateZExt(cmp, expected_type);
        return cmp;
      }
      case BinOp::Le: {
        Value *cmp = Builder.CreateICmpSLE(l, r);
        if (expected_type && !expected_type->isIntegerTy(1))
          return Builder.CreateZExt(cmp, expected_type);
        return cmp;
      }
      case BinOp::Ge: {
        Value *cmp = Builder.CreateICmpSGE(l, r);
        if (expected_type && !expected_type->isIntegerTy(1))
          return Builder.CreateZExt(cmp, expected_type);
        return cmp;
      }
      case BinOp::And: {
        // Short-circuit: if l is 0 (falsy), skip evaluating r and return 0.
        // If l is non-zero, evaluate r and return the truthiness of both.
        Function *fn = Builder.GetInsertBlock()->getParent();
        BasicBlock *rhs_bb = BasicBlock::Create(Context, "and.rhs", fn);
        BasicBlock *merge_bb = BasicBlock::Create(Context, "and.merge", fn);

        // Store the default result (false for the false branch) before branching.
        // The rhs_bb will overwrite this if l is truthy.
        AllocaInst *result_alloca = Builder.CreateAlloca(Type::getInt1Ty(Context), nullptr, "and.result");
        Builder.CreateStore(ConstantInt::getFalse(Context), result_alloca);

        Value *l_bool = Builder.CreateICmpNE(l, ConstantInt::get(l->getType(), 0));
        Builder.CreateCondBr(l_bool, rhs_bb, merge_bb);

        // Right-hand side block (only reached when l is truthy)
        Builder.SetInsertPoint(rhs_bb);
        Value *r_val = eval_expr(bin->right.get(), expected_type);
        if (!r_val) return nullptr;
        Value *r_bool = Builder.CreateICmpNE(r_val, ConstantInt::get(r_val->getType(), 0));
        Builder.CreateStore(r_bool, result_alloca);
        Builder.CreateBr(merge_bb);

        // Merge block: load the result (either 0 from the false path or r_bool from rhs_bb)
        Builder.SetInsertPoint(merge_bb);
        Value *result = Builder.CreateLoad(Type::getInt1Ty(Context), result_alloca);
        if (expected_type && !expected_type->isIntegerTy(1))
          return Builder.CreateZExt(result, expected_type);
        return result;
      }
      case BinOp::Or: {
        Value *lb = Builder.CreateICmpNE(l, ConstantInt::get(l->getType(), 0));
        Value *rb = Builder.CreateICmpNE(r, ConstantInt::get(r->getType(), 0));
        Value *cmp = Builder.CreateOr(lb, rb);
        if (expected_type && !expected_type->isIntegerTy(1))
          return Builder.CreateZExt(cmp, expected_type);
        return cmp;
      }
      case BinOp::Shr:
        return Builder.CreateAShr(l, r);
      case BinOp::Shl:
        return Builder.CreateShl(l, r);
    }
  }

  if (auto *call = dynamic_cast<CallExpr *>(expr)) {
    Function *callee = M.getFunction(call->callee);
    if (!callee) {
      errs() << "Error: undefined function '" << call->callee << "'\n";
      return nullptr;
    }
    size_t fixed_params = callee->arg_size();
    if (callee->isVarArg()) {
      if (call->args.size() < fixed_params) {
        errs() << "Error: too few arguments to '" << call->callee << "'\n";
        return nullptr;
      }
    } else if (fixed_params != call->args.size()) {
      errs() << "Error: wrong number of arguments to '" << call->callee << "'\n";
      return nullptr;
    }
    std::vector<Value *> args;
    for (size_t i = 0; i < call->args.size(); i++) {
      Type *param_type;
      if (i < fixed_params) {
        param_type = callee->getArg(i)->getType();
      } else {
        // Extra arguments past the declared parameters only happen for a
        // variadic (`...`) callee, which has no declared LLVM type for
        // them — infer a reasonable type from the argument expression
        // itself, matching common C variadic usage (e.g. printf).
        // Note: this does not perform full C variadic promotion rules
        // (float -> double, etc.); pass float64 explicitly if needed.
        if (dynamic_cast<StringExpr *>(call->args[i].get())) {
          param_type = PointerType::getUnqual(Context);
        } else {
          param_type = Type::getInt64Ty(Context);
        }
      }
      Value *arg = eval_expr(call->args[i].get(), param_type);
      if (!arg) return nullptr;
      // Array-to-pointer decay: if the argument is an array variable and
      // the parameter is a pointer, the eval_expr above already handles it.
      args.push_back(arg);
    }
    return Builder.CreateCall(callee, args);
  }

  if (auto *asm_ = dynamic_cast<AsmExpr *>(expr)) {
    FunctionType *FT = FunctionType::get(Type::getVoidTy(Context), false);
    InlineAsm *IA = InlineAsm::get(FT, asm_->asm_code, "", true, false);
    Builder.CreateCall(IA);
    return ConstantInt::get(Type::getInt64Ty(Context), 0);
  }

  if (auto *assign = dynamic_cast<AssignExpr *>(expr)) {
    Value *val = eval_expr(assign->value.get(), expected_type);
    if (!val) return nullptr;

    Type *target_type = nullptr;
    Value *target_ptr = get_lvalue_ptr(assign->target.get(), &target_type);
    if (!target_ptr) {
      errs() << "Error: invalid assignment target\n";
      return nullptr;
    }
    Builder.CreateStore(val, target_ptr);
    return val;
  }

  errs() << "Error: unknown expression type\n";
  return nullptr;
}

// -------------------------------------------------------------------------
// Per-type value generators
// -------------------------------------------------------------------------

Value *CodeGen::eval_int_init(Expr *expr) {
  if (auto *id = dynamic_cast<IdentExpr *>(expr)) {
    auto it = named_values.find(id->name);
    if (it == named_values.end()) {
      errs() << "Error: undefined variable '" << id->name << "'\n";
      return nullptr;
    }
    return Builder.CreateLoad(Type::getInt64Ty(Context), it->second, id->name);
  }
  return eval_expr(expr, Type::getInt64Ty(Context));
}

Value *CodeGen::eval_float_init(Expr *expr) {
  return eval_expr(expr, Type::getDoubleTy(Context));
}

Value *CodeGen::eval_string_init(Expr *expr) {
  return eval_expr(expr, PointerType::getUnqual(Context));
}

Value *CodeGen::eval_cubical_init(Expr *expr, std::string *debug_out) {
  auto *str = dynamic_cast<StringExpr *>(expr);
  if (!str) {
    errs() << "Error: cubical variable requires a string (inline source or file path)\n";
    return nullptr;
  }

  std::string cubical_source = str->value;

  if (cubical_source.size() >= 4 &&
      (cubical_source.substr(cubical_source.size() - 4) == ".cub")) {
    std::ifstream ifs(cubical_source);
    if (!ifs) {
      errs() << "Error: cannot open cubical file '" << cubical_source << "'\n";
      return nullptr;
    }
    cubical_source.assign((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
  }

  cubical_value cv(cubical_source);
  if (!cv.valid()) {
    errs() << "Error: cubical evaluation failed\n";
    return nullptr;
  }

  int64_t int_val = cv.as_int();
  if (int_val >= 0) {
    if (debug_out)
      *debug_out = std::to_string(int_val) + "  (from cubical: " + cv.str() + ")";
    return ConstantInt::get(Type::getInt64Ty(Context), int_val);
  }

  if (debug_out)
    *debug_out = "\"" + cv.str() + "\"";
  return Builder.CreateGlobalString(cv.str(), "cubical_result");
}

// -------------------------------------------------------------------------
// Array initialization helpers
// -------------------------------------------------------------------------

Value *CodeGen::eval_array_init(Expr *expr, ArrayType *array_type) {
  if (auto *arr_lit = dynamic_cast<ArrayLitExpr *>(expr)) {
    return eval_array_literal(arr_lit, array_type);
  }
  // Fallback: zero-initialize
  return ConstantAggregateZero::get(array_type);
}

Value *CodeGen::eval_array_literal(ArrayLitExpr *arr, ArrayType *array_type) {
  Type *elem_type = array_type->getElementType();
  unsigned num_elems = array_type->getNumElements();

  // Check if all elements are compile-time constants
  bool all_const = true;
  for (size_t i = 0; i < arr->elements.size() && i < num_elems; i++) {
    if (!dynamic_cast<NumberExpr *>(arr->elements[i].get())) {
      all_const = false;
      break;
    }
  }

  if (all_const) {
    // Fast path: constant array
    std::vector<Constant *> init_vals;
    for (size_t i = 0; i < arr->elements.size() && i < num_elems; i++) {
      auto *num = static_cast<NumberExpr *>(arr->elements[i].get());
      if (elem_type->isFPOrFPVectorTy())
        init_vals.push_back(ConstantFP::get(elem_type, num->value));
      else
        init_vals.push_back(ConstantInt::get(elem_type, (int64_t)num->value));
    }
    while (init_vals.size() < num_elems)
      init_vals.push_back(Constant::getNullValue(elem_type));
    return ConstantArray::get(array_type, init_vals);
  }

  // Runtime path: build array element by element using InsertValue
  Value *arr_val = ConstantAggregateZero::get(array_type);
  for (size_t i = 0; i < arr->elements.size() && i < num_elems; i++) {
    Value *el = eval_expr(arr->elements[i].get(), elem_type);
    if (!el) return nullptr;
    arr_val = Builder.CreateInsertValue(arr_val, el, {(unsigned)i}, "arr.init");
  }
  return arr_val;
}

// -------------------------------------------------------------------------
// Alloca + store helper
// -------------------------------------------------------------------------

bool CodeGen::alloc_and_store(const std::string &name, TypeKind kind,
                               Value *init, Type *llvm_type,
                               TypeAnnotation ann) {
  AllocaInst *alloca = Builder.CreateAlloca(llvm_type, nullptr, name);
  Builder.CreateStore(init, alloca);
  named_values[name] = alloca;
  named_types[name] = kind;
  if (kind == TypeKind::Struct || kind == TypeKind::Enum || ann.pointer_depth > 0)
    named_type_anns[name] = ann;
  return true;
}

bool CodeGen::alloc_and_store_array(const std::string &name, TypeKind kind,
                                     int array_size, ArrayType *array_type,
                                     Value *init,
                                     TypeAnnotation ann) {
  AllocaInst *alloca = Builder.CreateAlloca(array_type, nullptr, name);
  Builder.CreateStore(init, alloca);
  named_values[name] = alloca;
  named_types[name] = kind;
  if (kind == TypeKind::Struct || kind == TypeKind::Enum || ann.array_size > 0)
    named_type_anns[name] = ann;
  return true;
}

// -------------------------------------------------------------------------
// Let declarations (top-level)
// -------------------------------------------------------------------------

bool CodeGen::gen_let_decl(LetDecl *decl) {
  Type *llvm_type = get_llvm_type(decl->type_ann);
  if (!llvm_type) return false;

  // Handle array types
  if (decl->type_ann.array_size > 0) {
    ArrayType *arr_type = cast<ArrayType>(llvm_type);
    Value *init = eval_array_init(decl->init_expr.get(), arr_type);
    if (!init) return false;
    return alloc_and_store_array(decl->name, decl->type_ann.kind,
                                  decl->type_ann.array_size, arr_type, init,
                                  decl->type_ann);
  }

  // Handle struct types
  if (decl->type_ann.kind == TypeKind::Struct) {
    Value *init = nullptr;
    if (decl->init_expr) {
      // If the init is an IdentExpr referencing an undefined name (the
      // placeholder convention `let p: Point = p`), skip evaluation and
      // zero-initialize. Otherwise try to evaluate it — it might be
      // another struct variable, a function call, etc.
      bool skip = false;
      if (auto *id = dynamic_cast<IdentExpr *>(decl->init_expr.get()))
        skip = named_values.find(id->name) == named_values.end();
      if (!skip)
        init = eval_expr(decl->init_expr.get(), llvm_type);
    }
    if (!init)
      init = ConstantAggregateZero::get(llvm_type);
    return alloc_and_store(decl->name, decl->type_ann.kind, init, llvm_type,
                            decl->type_ann);
  }

  Value *init = nullptr;
  std::string debug;

  if (decl->type_ann.pointer_depth > 0) {
    init = eval_expr(decl->init_expr.get(), llvm_type);
    if (!init) return false;
    return alloc_and_store(decl->name, decl->type_ann.kind, init, llvm_type,
                            decl->type_ann);
  }

  switch (decl->type_ann.kind) {
    case TypeKind::Void:
      errs() << "Error: variable cannot have void type\n";
      return false;
    case TypeKind::Int8:
      init = eval_expr(decl->init_expr.get(), Type::getInt8Ty(Context));
      break;
    case TypeKind::Int32:
      init = eval_expr(decl->init_expr.get(), Type::getInt32Ty(Context));
      break;
    case TypeKind::Int64:
      init = eval_int_init(decl->init_expr.get());
      break;
    case TypeKind::Float16:
      init = eval_expr(decl->init_expr.get(), Type::getHalfTy(Context));
      break;
    case TypeKind::Float32:
      init = eval_expr(decl->init_expr.get(), Type::getFloatTy(Context));
      break;
    case TypeKind::Float64:
      init = eval_float_init(decl->init_expr.get());
      break;
    case TypeKind::Bool:
      init = eval_expr(decl->init_expr.get(), Type::getInt1Ty(Context));
      break;
    case TypeKind::String:
      init = eval_string_init(decl->init_expr.get());
      break;
    case TypeKind::Cubical:
      init = eval_cubical_init(decl->init_expr.get(), &debug);
      if (init) std::cout << "  " << decl->name << " = " << debug << "\n";
      break;
    default:
      break;
  }
  if (!init) return false;
  return alloc_and_store(decl->name, decl->type_ann.kind, init, llvm_type,
                          decl->type_ann);
}

bool CodeGen::gen_let_stmt(LetStmt *stmt) {
  Type *llvm_type = get_llvm_type(stmt->type_ann);
  if (!llvm_type) return false;

  // Handle array types
  if (stmt->type_ann.array_size > 0) {
    ArrayType *arr_type = cast<ArrayType>(llvm_type);
    Value *init = eval_array_init(stmt->init_expr.get(), arr_type);
    if (!init) return false;
    return alloc_and_store_array(stmt->name, stmt->type_ann.kind,
                                  stmt->type_ann.array_size, arr_type, init,
                                  stmt->type_ann);
  }

  // Handle struct types
  if (stmt->type_ann.kind == TypeKind::Struct) {
    Value *init = nullptr;
    if (stmt->init_expr) {
      bool skip = false;
      if (auto *id = dynamic_cast<IdentExpr *>(stmt->init_expr.get()))
        skip = named_values.find(id->name) == named_values.end();
      if (!skip)
        init = eval_expr(stmt->init_expr.get(), llvm_type);
    }
    if (!init)
      init = ConstantAggregateZero::get(llvm_type);
    return alloc_and_store(stmt->name, stmt->type_ann.kind, init, llvm_type,
                            stmt->type_ann);
  }

  Value *init = nullptr;

  if (stmt->type_ann.pointer_depth > 0) {
    init = eval_expr(stmt->init_expr.get(), llvm_type);
    if (!init) return false;
    return alloc_and_store(stmt->name, stmt->type_ann.kind, init, llvm_type,
                            stmt->type_ann);
  }

  switch (stmt->type_ann.kind) {
    case TypeKind::Void:
      errs() << "Error: variable cannot have void type\n";
      return false;
    case TypeKind::Int8:
      init = eval_expr(stmt->init_expr.get(), Type::getInt8Ty(Context));
      break;
    case TypeKind::Int32:
      init = eval_expr(stmt->init_expr.get(), Type::getInt32Ty(Context));
      break;
    case TypeKind::Int64:
      init = eval_int_init(stmt->init_expr.get());
      break;
    case TypeKind::Float16:
      init = eval_expr(stmt->init_expr.get(), Type::getHalfTy(Context));
      break;
    case TypeKind::Float32:
      init = eval_expr(stmt->init_expr.get(), Type::getFloatTy(Context));
      break;
    case TypeKind::Float64:
      init = eval_float_init(stmt->init_expr.get());
      break;
    case TypeKind::Bool:
      init = eval_expr(stmt->init_expr.get(), Type::getInt1Ty(Context));
      break;
    case TypeKind::String:
      init = eval_string_init(stmt->init_expr.get());
      break;
    case TypeKind::Cubical:
      init = eval_cubical_init(stmt->init_expr.get(), nullptr);
      break;
    default:
      break;
  }
  if (!init) return false;
  return alloc_and_store(stmt->name, stmt->type_ann.kind, init, llvm_type,
                          stmt->type_ann);
}

// -------------------------------------------------------------------------
// Functions
// -------------------------------------------------------------------------

bool CodeGen::gen_fn_body(FnDecl *decl, Function *fn) {
  BasicBlock *BB = BasicBlock::Create(Context, "entry", fn);
  Builder.SetInsertPoint(BB);

  // Save outer scope
  auto saved_values = std::move(named_values);
  auto saved_types = std::move(named_types);
  auto saved_type_anns = std::move(named_type_anns);
  named_values.clear();
  named_types.clear();
  named_type_anns.clear();

  // Allocate and store parameters
  size_t i = 0;
  for (auto &arg : fn->args()) {
    arg.setName(decl->params[i].name);
    AllocaInst *alloca = Builder.CreateAlloca(arg.getType(), nullptr, arg.getName());
    Builder.CreateStore(&arg, alloca);
    std::string pname = std::string(arg.getName());
    named_values[pname] = alloca;
    named_types[pname] = decl->params[i].type_ann.kind;
    {
      auto &ta = decl->params[i].type_ann;
      if (ta.kind == TypeKind::Struct || ta.kind == TypeKind::Enum || ta.pointer_depth > 0 || ta.array_size > 0)
        named_type_anns[pname] = ta;
    }
    i++;
  }

  // Generate body
  for (auto &stmt : decl->body) {
      if (!gen_stmt(stmt.get())) {
        named_values = std::move(saved_values);
        named_types = std::move(saved_types);
        named_type_anns = std::move(saved_type_anns);
        return false;
      }
  }

  // If function doesn't return, add a default return
  Type *ret_type = fn->getReturnType();
  if (!Builder.GetInsertBlock()->getTerminator()) {
    if (ret_type->isVoidTy())
      Builder.CreateRetVoid();
    else if (ret_type->isIntegerTy(64))
      Builder.CreateRet(ConstantInt::get(ret_type, 0));
    else if (ret_type->isFPOrFPVectorTy())
      Builder.CreateRet(ConstantFP::get(ret_type, 0.0));
    else
      Builder.CreateRet(ConstantPointerNull::get(cast<PointerType>(ret_type)));
  }

  // Restore scope
  named_values = std::move(saved_values);
  named_types = std::move(saved_types);
  named_type_anns = std::move(saved_type_anns);
  return true;
}

// -------------------------------------------------------------------------
// Statements
// -------------------------------------------------------------------------

bool CodeGen::gen_stmt(Stmt *stmt) {
  if (auto *let = dynamic_cast<LetStmt *>(stmt))
    return gen_let_stmt(let);
  if (auto *ret = dynamic_cast<ReturnStmt *>(stmt))
    return gen_return_stmt(ret);
  if (auto *if_ = dynamic_cast<IfStmt *>(stmt))
    return gen_if_stmt(if_);
  if (auto *for_ = dynamic_cast<ForStmt *>(stmt))
    return gen_for_stmt(for_);
  if (auto *expr = dynamic_cast<ExprStmt *>(stmt)) {
    Value *v = eval_expr(expr->expr.get(), Type::getInt64Ty(Context));
    return v != nullptr;
  }
  errs() << "Error: unknown statement type\n";
  return false;
}

bool CodeGen::gen_return_stmt(ReturnStmt *stmt) {
  Function *fn = Builder.GetInsertBlock()->getParent();
  Type *ret_type = fn->getReturnType();

  if (ret_type->isVoidTy()) {
    Builder.CreateRetVoid();
    return true;
  }

  if (!stmt->value) {
    errs() << "Error: non-void function must return a value\n";
    return false;
  }

  Value *val = eval_expr(stmt->value.get(), ret_type);
  if (!val) return false;
  Builder.CreateRet(val);
  return true;
}

bool CodeGen::gen_if_stmt(IfStmt *stmt) {
  Function *fn = Builder.GetInsertBlock()->getParent();

  Value *cond = eval_expr(stmt->condition.get(), nullptr);
  if (!cond) return false;
  if (!cond->getType()->isIntegerTy(1))
    cond = Builder.CreateICmpNE(cond, ConstantInt::get(cond->getType(), 0));

  BasicBlock *then_bb = BasicBlock::Create(Context, "if.then", fn);
  BasicBlock *else_bb = BasicBlock::Create(Context, "if.else", fn);
  BasicBlock *merge_bb = BasicBlock::Create(Context, "if.end", fn);

  Builder.CreateCondBr(cond, then_bb, else_bb);

  Builder.SetInsertPoint(then_bb);
  for (auto &s : stmt->then_branch) {
    if (!gen_stmt(s.get())) return false;
  }
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(merge_bb);

  Builder.SetInsertPoint(else_bb);
  for (auto &s : stmt->else_branch) {
    if (!gen_stmt(s.get())) return false;
  }
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(merge_bb);

  Builder.SetInsertPoint(merge_bb);
  return true;
}

bool CodeGen::gen_for_stmt(ForStmt *stmt) {
  Function *fn = Builder.GetInsertBlock()->getParent();

  if (stmt->init) {
    if (!gen_stmt(stmt->init.get())) return false;
  }

  BasicBlock *cond_bb = BasicBlock::Create(Context, "for.cond", fn);
  BasicBlock *body_bb = BasicBlock::Create(Context, "for.body", fn);
  BasicBlock *update_bb = BasicBlock::Create(Context, "for.update", fn);
  BasicBlock *end_bb = BasicBlock::Create(Context, "for.end", fn);

  Builder.CreateBr(cond_bb);

  Builder.SetInsertPoint(cond_bb);
  if (stmt->condition) {
    Value *cond = eval_expr(stmt->condition.get(), nullptr);
    if (!cond) return false;
    if (!cond->getType()->isIntegerTy(1))
      cond = Builder.CreateICmpNE(cond, ConstantInt::get(cond->getType(), 0));
    Builder.CreateCondBr(cond, body_bb, end_bb);
  } else {
    Builder.CreateBr(body_bb);
  }

  Builder.SetInsertPoint(body_bb);
  for (auto &s : stmt->body) {
    if (!gen_stmt(s.get())) return false;
  }
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(update_bb);

  Builder.SetInsertPoint(update_bb);
  if (stmt->update) {
    Value *v = eval_expr(stmt->update.get(), Type::getInt64Ty(Context));
    if (!v) return false;
  }
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(cond_bb);

  Builder.SetInsertPoint(end_bb);
  return true;
}

// -------------------------------------------------------------------------
// Pattern matching helpers
// -------------------------------------------------------------------------

Value *CodeGen::gen_pattern_check(Pattern *pat, Value *val,
                                   const TypeAnnotation &val_ann) {
  if (auto *wc = dynamic_cast<WildcardPattern *>(pat)) {
    return ConstantInt::getTrue(Context);
  }

  if (auto *lit = dynamic_cast<LiteralPattern *>(pat)) {
    Value *lit_val = eval_expr(lit->value.get(), val->getType());
    if (!lit_val) return nullptr;
    if (val->getType()->isIntegerTy())
      return Builder.CreateICmpEQ(val, lit_val);
    if (val->getType()->isFPOrFPVectorTy())
      return Builder.CreateFCmpOEQ(val, lit_val);
    if (val->getType()->isPointerTy())
      return Builder.CreateICmpEQ(val, lit_val);
    errs() << "Error: unsupported type for literal pattern\n";
    return nullptr;
  }

  if (auto *var = dynamic_cast<VariablePattern *>(pat)) {
    // Check if this is a unit variant of an enum
    bool is_enum = (val_ann.kind == TypeKind::Enum) ||
                   (val_ann.kind == TypeKind::Struct &&
                    enum_types.find(val_ann.struct_name) != enum_types.end());
    if (is_enum) {
      int vi = get_enum_variant_index(val_ann.struct_name, var->name);
      if (vi >= 0) {
        // Unit variant: check tag
        Value *tag = Builder.CreateExtractValue(val, {0}, "tag");
        return Builder.CreateICmpEQ(tag,
            ConstantInt::get(Type::getInt64Ty(Context), vi));
      }
    }
    return ConstantInt::getTrue(Context);
  }

  if (auto *sp = dynamic_cast<StructPattern *>(pat)) {
    // Check if this is actually an enum variant pattern
    bool is_variant = (val_ann.kind == TypeKind::Enum) ||
                      (val_ann.kind == TypeKind::Struct &&
                       enum_types.find(val_ann.struct_name) != enum_types.end());
    int variant_data_idx = -1;

    if (is_variant) {
      // Look up variant index within the enum
      int vi = get_enum_variant_index(val_ann.struct_name, sp->struct_name);
      if (vi < 0) {
        errs() << "Error: '" << sp->struct_name << "' is not a variant of enum '"
               << val_ann.struct_name << "'\n";
        return nullptr;
      }
      variant_data_idx = 1 + vi; // index 0 = tag, 1..N = variant data

      // First check the tag matches this variant
      Value *tag = Builder.CreateExtractValue(val, {0}, "tag");
      Value *tag_match = Builder.CreateICmpEQ(tag,
          ConstantInt::get(Type::getInt64Ty(Context), vi));
      Value *cond = tag_match;

      // Then check field sub-patterns from the variant data
      for (auto &[field_name, sub_pat] : sp->fields) {
        int idx = get_struct_field_index(
            val_ann.struct_name + "::" + sp->struct_name, field_name);
        if (idx < 0) {
          errs() << "Error: variant '" << sp->struct_name
                 << "' has no field '" << field_name << "'\n";
          return nullptr;
        }
        Value *field_val = Builder.CreateExtractValue(val,
            {(unsigned)variant_data_idx, (unsigned)idx}, field_name);
        TypeAnnotation field_ann = get_struct_field_type(
            val_ann.struct_name + "::" + sp->struct_name, field_name);
        Value *sub_cond = gen_pattern_check(sub_pat.get(), field_val, field_ann);
        if (!sub_cond) return nullptr;
        cond = Builder.CreateAnd(cond, sub_cond);
      }
      return cond;
    }

    // Regular struct pattern
    StructType *st = dyn_cast<StructType>(val->getType());
    if (!st) {
      errs() << "Error: struct pattern on non-struct value\n";
      return nullptr;
    }
    Value *cond = ConstantInt::getTrue(Context);
    for (auto &[field_name, sub_pat] : sp->fields) {
      int idx = get_struct_field_index(sp->struct_name, field_name);
      if (idx < 0) {
        errs() << "Error: struct '" << sp->struct_name << "' has no field '"
               << field_name << "'\n";
        return nullptr;
      }
      Value *field_val = Builder.CreateExtractValue(val, {(unsigned)idx}, field_name);
      TypeAnnotation field_ann = get_struct_field_type(sp->struct_name, field_name);
      Value *sub_cond = gen_pattern_check(sub_pat.get(), field_val, field_ann);
      if (!sub_cond) return nullptr;
      cond = Builder.CreateAnd(cond, sub_cond);
    }
    return cond;
  }

  errs() << "Error: unknown pattern type\n";
  return nullptr;
}

bool CodeGen::gen_pattern_bind(Pattern *pat, Value *val,
                                const TypeAnnotation &val_ann) {
  if (auto *wc = dynamic_cast<WildcardPattern *>(pat)) {
    return true;
  }

  if (auto *lit = dynamic_cast<LiteralPattern *>(pat)) {
    return true;
  }

  if (auto *var = dynamic_cast<VariablePattern *>(pat)) {
    Type *ty = val->getType();
    AllocaInst *alloca = Builder.CreateAlloca(ty, nullptr, var->name);
    named_values[var->name] = alloca;
    Builder.CreateStore(val, alloca);
    return true;
  }

  if (auto *sp = dynamic_cast<StructPattern *>(pat)) {
    bool is_variant = (val_ann.kind == TypeKind::Enum) ||
                      (val_ann.kind == TypeKind::Struct &&
                       enum_types.find(val_ann.struct_name) != enum_types.end());
    int variant_data_idx = -1;

    if (is_variant) {
      int vi = get_enum_variant_index(val_ann.struct_name, sp->struct_name);
      if (vi < 0) return false;
      variant_data_idx = 1 + vi;
    }

    for (auto &[field_name, sub_pat] : sp->fields) {
      Value *field_val;
      TypeAnnotation field_ann;
      if (is_variant) {
        int idx = get_struct_field_index(
            val_ann.struct_name + "::" + sp->struct_name, field_name);
        if (idx < 0) return false;
        field_val = Builder.CreateExtractValue(val,
            {(unsigned)variant_data_idx, (unsigned)idx}, field_name);
        field_ann = get_struct_field_type(
            val_ann.struct_name + "::" + sp->struct_name, field_name);
      } else {
        int idx = get_struct_field_index(sp->struct_name, field_name);
        if (idx < 0) return false;
        field_val = Builder.CreateExtractValue(val, {(unsigned)idx}, field_name);
        field_ann = get_struct_field_type(sp->struct_name, field_name);
      }
      if (!gen_pattern_bind(sub_pat.get(), field_val, field_ann))
        return false;
    }
    return true;
  }

  errs() << "Error: unknown pattern type in bind\n";
  return false;
}