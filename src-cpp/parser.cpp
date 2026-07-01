#include "parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>

void Parser::next_token() {
  cur_tok = lexer.next_token();
}

void Parser::skip_newlines() {
  while (cur_tok.type == TokenType::Newline)
    cur_tok = lexer.next_token();
}

void Parser::set_error(const std::string &msg) {
  has_error = true;
  error_msg = msg + " at line " + std::to_string(cur_tok.line) + ":" +
              std::to_string(cur_tok.col);
}

// -------------------------------------------------------------------------
// Top-level
// -------------------------------------------------------------------------

std::vector<std::unique_ptr<Decl>> Parser::parse_program() {
  std::vector<std::unique_ptr<Decl>> decls;
  skip_newlines();
  while (cur_tok.type != TokenType::Eof) {
    if (cur_tok.type == TokenType::Let) {
      auto decl = parse_let_decl();
      if (decl) decls.push_back(std::move(decl));
      else break;
    } else if (cur_tok.type == TokenType::Fn) {
      auto decl = parse_fn_decl();
      if (decl) decls.push_back(std::move(decl));
      else break;
    } else if (cur_tok.type == TokenType::Struct) {
      auto decl = parse_struct_decl();
      if (decl) decls.push_back(std::move(decl));
      else break;
    } else if (cur_tok.type == TokenType::Enum) {
      auto decl = parse_enum_decl();
      if (decl) decls.push_back(std::move(decl));
      else break;
    } else if (cur_tok.type == TokenType::Include) {
      if (!parse_include_decl(decls)) break;
    } else if (cur_tok.type == TokenType::Namespace) {
      if (!parse_namespace_decl(decls)) break;
    } else if (cur_tok.type == TokenType::Extern) {
      auto decl = parse_extern_fn_decl();
      if (decl) decls.push_back(std::move(decl));
      else break;
    } else {
      set_error("expected declaration (let, fn, struct, enum, include, namespace, extern)");
      break;
    }
    skip_newlines();
  }
  return decls;
}

// -------------------------------------------------------------------------
// Type annotations
// -------------------------------------------------------------------------

TypeAnnotation Parser::parse_type_annotation() {
  TypeAnnotation ann;
  if (cur_tok.type == TokenType::Void) {
    ann = {TypeKind::Void};
    next_token();
  } else if (cur_tok.type == TokenType::Int8) {
    ann = {TypeKind::Int8};
    next_token();
  } else if (cur_tok.type == TokenType::Int32) {
    ann = {TypeKind::Int32};
    next_token();
  } else if (cur_tok.type == TokenType::Int64) {
    ann = {TypeKind::Int64};
    next_token();
  } else if (cur_tok.type == TokenType::Float16) {
    ann = {TypeKind::Float16};
    next_token();
  } else if (cur_tok.type == TokenType::Float32) {
    ann = {TypeKind::Float32};
    next_token();
  } else if (cur_tok.type == TokenType::Float64) {
    ann = {TypeKind::Float64};
    next_token();
  } else if (cur_tok.type == TokenType::Bool) {
    ann = {TypeKind::Bool};
    next_token();
  } else if (cur_tok.type == TokenType::String) {
    ann = {TypeKind::String};
    next_token();
  } else if (cur_tok.type == TokenType::Cubical) {
    ann = {TypeKind::Cubical};
    next_token();
  } else if (cur_tok.type == TokenType::Identifier) {
    // Struct type: the identifier is the struct name (possibly namespaced,
    // e.g. foo::Point or a::b::Point).
    ann = {TypeKind::Struct};
    ann.struct_name = cur_tok.text;
    next_token();
    while (cur_tok.type == TokenType::ColonColon) {
      next_token(); // consume '::'
      if (cur_tok.type != TokenType::Identifier) {
        set_error("expected identifier after '::' in type name");
        return ann;
      }
      ann.struct_name += "::" + cur_tok.text;
      next_token();
    }
  } else {
    set_error("expected type (void, int8, int32, int64, float, bool, string, cubical, or struct name)");
    ann = {TypeKind::Int64};
    has_error = true;
    return ann;
  }

  // Parse pointer indirection levels (e.g. int* -> pointer to int)
  while (cur_tok.type == TokenType::Star) {
    ann.pointer_depth++;
    next_token();
  }

  // Parse array size (e.g. int[10])
  if (cur_tok.type == TokenType::LSquare) {
    next_token(); // consume '['
    if (cur_tok.type != TokenType::Number) {
      set_error("expected array size as number literal");
      return ann;
    }
    ann.array_size = (int)cur_tok.num_val;
    next_token(); // consume number
    if (cur_tok.type != TokenType::RSquare) {
      set_error("expected ']' after array size");
      return ann;
    }
    next_token(); // consume ']'
  }

  return ann;
}

// -------------------------------------------------------------------------
// Struct declarations
// -------------------------------------------------------------------------

std::unique_ptr<StructDecl> Parser::parse_struct_decl() {
  next_token(); // consume 'struct'

  if (cur_tok.type != TokenType::Identifier) {
    set_error("expected struct name");
    return nullptr;
  }
  std::string name = cur_tok.text;
  next_token();

  skip_newlines();
  if (cur_tok.type != TokenType::LBrace) {
    set_error("expected '{' after struct name");
    return nullptr;
  }
  next_token(); // consume '{'
  skip_newlines();

  std::vector<StructField> fields;
  while (cur_tok.type != TokenType::RBrace && cur_tok.type != TokenType::Eof) {
    StructField field;
    if (cur_tok.type != TokenType::Identifier) {
      set_error("expected field name");
      return nullptr;
    }
    field.name = cur_tok.text;
    next_token();

    if (cur_tok.type != TokenType::Colon) {
      set_error("expected ':' after field name");
      return nullptr;
    }
    next_token();

    field.type_ann = parse_type_annotation();
    if (has_error) return nullptr;

    fields.push_back(std::move(field));
    skip_newlines();
    if (cur_tok.type == TokenType::Comma) {
      next_token();
      skip_newlines();
    }
  }

  if (cur_tok.type != TokenType::RBrace) {
    set_error("expected '}' after struct fields");
    return nullptr;
  }
  next_token(); // consume '}'

  auto decl = std::make_unique<StructDecl>();
  decl->name = name;
  known_structs.insert(name);
  decl->fields = std::move(fields);
  return decl;
}

// -------------------------------------------------------------------------
// Enum declarations
// -------------------------------------------------------------------------

std::unique_ptr<AdtDecl> Parser::parse_enum_decl() {
  next_token(); // consume 'enum'

  if (cur_tok.type != TokenType::Identifier) {
    set_error("expected enum name");
    return nullptr;
  }
  std::string name = cur_tok.text;
  next_token();

  skip_newlines();
  if (cur_tok.type != TokenType::LBrace) {
    set_error("expected '{' after enum name");
    return nullptr;
  }
  next_token(); // consume '{'
  skip_newlines();

  std::vector<AdtVariant> variants;
  while (cur_tok.type != TokenType::RBrace && cur_tok.type != TokenType::Eof) {
    AdtVariant variant;
    if (cur_tok.type != TokenType::Identifier) {
      set_error("expected variant name");
      return nullptr;
    }
    variant.name = cur_tok.text;
    known_variants.insert(cur_tok.text);
    next_token();
    skip_newlines();

    // Variant with fields: Some { x: int, y: int }
    if (cur_tok.type == TokenType::LBrace) {
      next_token(); // consume '{'
      skip_newlines();
      while (cur_tok.type != TokenType::RBrace && cur_tok.type != TokenType::Eof) {
        StructField field;
        if (cur_tok.type != TokenType::Identifier) {
          set_error("expected field name in variant");
          return nullptr;
        }
        field.name = cur_tok.text;
        next_token();
        if (cur_tok.type != TokenType::Colon) {
          set_error("expected ':' after field name");
          return nullptr;
        }
        next_token();
        field.type_ann = parse_type_annotation();
        if (has_error) return nullptr;
        variant.fields.push_back(std::move(field));
        skip_newlines();
        if (cur_tok.type == TokenType::Comma) {
          next_token();
          skip_newlines();
        }
      }
      if (cur_tok.type != TokenType::RBrace) {
        set_error("expected '}' to close variant fields");
        return nullptr;
      }
      next_token(); // consume '}'
    }
    // else: unit variant (no fields)

    variants.push_back(std::move(variant));
    skip_newlines();
    if (cur_tok.type == TokenType::Comma) {
      next_token();
      skip_newlines();
    }
  }

  if (cur_tok.type != TokenType::RBrace) {
    set_error("expected '}' after enum variants");
    return nullptr;
  }
  next_token(); // consume '}'

  auto decl = std::make_unique<AdtDecl>();
  decl->name = name;
  decl->variants = std::move(variants);
  return decl;
}

// -------------------------------------------------------------------------
// Namespace declarations
// -------------------------------------------------------------------------
//
// Namespaces are resolved entirely here, at parse time: every declaration
// directly inside `namespace foo { ... }` has its name rewritten to
// "foo::name" before being handed back to the caller. Nested namespaces
// fall out for free, since each level only ever prefixes with its own
// name — `namespace a { namespace b { fn f() {} } }` first becomes
// `b::f` when the inner namespace returns, then `a::b::f` when the outer
// one does. Because this happens before codegen ever sees the AST, no
// codegen changes are needed: "a::b::f" is just a function name like any
// other, and `eval_expr`/`M.getFunction`/`struct_types[...]` etc. all key
// on plain strings already.

bool Parser::parse_namespace_decl(std::vector<std::unique_ptr<Decl>> &decls) {
  next_token(); // consume 'namespace'

  if (cur_tok.type != TokenType::Identifier) {
    set_error("expected namespace name");
    return false;
  }
  std::string ns_name = cur_tok.text;
  next_token();

  skip_newlines();
  if (cur_tok.type != TokenType::LBrace) {
    set_error("expected '{' after namespace name");
    return false;
  }
  next_token(); // consume '{'
  skip_newlines();

  std::vector<std::unique_ptr<Decl>> inner_decls;
  while (cur_tok.type != TokenType::RBrace && cur_tok.type != TokenType::Eof) {
    if (cur_tok.type == TokenType::Let) {
      auto decl = parse_let_decl();
      if (!decl) return false;
      inner_decls.push_back(std::move(decl));
    } else if (cur_tok.type == TokenType::Fn) {
      auto decl = parse_fn_decl();
      if (!decl) return false;
      inner_decls.push_back(std::move(decl));
    } else if (cur_tok.type == TokenType::Struct) {
      auto decl = parse_struct_decl();
      if (!decl) return false;
      inner_decls.push_back(std::move(decl));
    } else if (cur_tok.type == TokenType::Enum) {
      auto decl = parse_enum_decl();
      if (!decl) return false;
      inner_decls.push_back(std::move(decl));
    } else if (cur_tok.type == TokenType::Include) {
      if (!parse_include_decl(inner_decls)) return false;
    } else if (cur_tok.type == TokenType::Namespace) {
      if (!parse_namespace_decl(inner_decls)) return false;
    } else {
      set_error("expected declaration inside namespace (let, fn, struct, enum, include, namespace)");
      return false;
    }
    skip_newlines();
  }

  if (cur_tok.type != TokenType::RBrace) {
    set_error("expected '}' to close namespace '" + ns_name + "'");
    return false;
  }
  next_token(); // consume '}'

  for (auto &d : inner_decls) {
    if (auto *fn = dynamic_cast<FnDecl *>(d.get())) {
      fn->name = ns_name + "::" + fn->name;
    } else if (auto *let = dynamic_cast<LetDecl *>(d.get())) {
      let->name = ns_name + "::" + let->name;
    } else if (auto *st = dynamic_cast<StructDecl *>(d.get())) {
      st->name = ns_name + "::" + st->name;
    } else if (auto *adt = dynamic_cast<AdtDecl *>(d.get())) {
      adt->name = ns_name + "::" + adt->name;
    }
    decls.push_back(std::move(d));
  }
  return true;
}

// -------------------------------------------------------------------------
// Include declarations
// -------------------------------------------------------------------------

bool Parser::parse_include_decl(std::vector<std::unique_ptr<Decl>> &decls) {
  next_token(); // consume 'include'

  if (cur_tok.type != TokenType::StringLiteral) {
    set_error("expected string literal path after 'include'");
    return false;
  }
  std::string raw_path = cur_tok.text;
  next_token();

  namespace fs = std::filesystem;
  fs::path requested(raw_path);
  fs::path full_path = requested.is_absolute()
                            ? requested
                            : fs::path(base_dir.empty() ? "." : base_dir) / requested;

  std::error_code ec;
  fs::path canonical = fs::weakly_canonical(full_path, ec);
  if (ec) canonical = full_path;

  std::ifstream ifs(full_path);
  if (!ifs) {
    set_error("cannot open included file '" + raw_path + "'");
    return false;
  }

  // Skip files we've already pulled in (directly or transitively), so
  // diamond includes don't duplicate declarations and self/mutual
  // includes don't recurse forever.
  std::string key = canonical.string();
  if (included_files->count(key)) {
    return true;
  }
  included_files->insert(key);

  std::stringstream ss;
  ss << ifs.rdbuf();
  std::string content = ss.str();

  Lexer sub_lexer(content);
  Parser sub_parser(sub_lexer, full_path.parent_path().string(), included_files);
  auto sub_decls = sub_parser.parse_program();

  if (!sub_parser.ok()) {
    set_error("in included file '" + raw_path + "': " + sub_parser.error());
    return false;
  }

  for (auto &d : sub_decls) {
    decls.push_back(std::move(d));
  }
  return true;
}

// -------------------------------------------------------------------------
// Let declarations (top-level)
// -------------------------------------------------------------------------

bool Parser::parse_let_common(TypeAnnotation &ann, std::string &name,
                               std::unique_ptr<Expr> &init) {
  if (cur_tok.type != TokenType::Identifier) {
    set_error("expected variable name");
    return false;
  }
  name = cur_tok.text;
  next_token();

  if (cur_tok.type != TokenType::Colon) {
    set_error("expected ':' after variable name");
    return false;
  }
  next_token();

  ann = parse_type_annotation();
  if (has_error) return false;

  if (ann.kind == TypeKind::Void) {
    set_error("variable cannot have void type");
    return false;
  }

  if (cur_tok.type != TokenType::Equals) {
    set_error("expected '=' after type");
    return false;
  }
  next_token();

  init = parse_expr();
  if (!init && !has_error) {
    set_error("expected expression");
    return false;
  }
  return !has_error;
}

std::unique_ptr<LetDecl> Parser::parse_let_decl() {
  next_token(); // consume 'let'

  TypeAnnotation type_ann;
  std::string name;
  std::unique_ptr<Expr> init;
  if (!parse_let_common(type_ann, name, init)) return nullptr;

  auto decl = std::make_unique<LetDecl>();
  decl->type_ann = type_ann;
  decl->name = name;
  decl->init_expr = std::move(init);
  return decl;
}

// -------------------------------------------------------------------------
// Function declarations
// -------------------------------------------------------------------------

std::unique_ptr<FnDecl> Parser::parse_fn_decl() {
  next_token(); // consume 'fn'

  if (cur_tok.type != TokenType::Identifier) {
    set_error("expected function name");
    return nullptr;
  }
  std::string name = cur_tok.text;
  next_token();

  if (cur_tok.type != TokenType::LParen) {
    set_error("expected '(' after function name");
    return nullptr;
  }
  next_token();

  std::vector<Param> params;
  while (cur_tok.type != TokenType::RParen) {
    if (!params.empty()) {
      if (cur_tok.type != TokenType::Comma) {
        set_error("expected ',' or ')' in parameter list");
        return nullptr;
      }
      next_token();
    }
    Param param;
    if (cur_tok.type != TokenType::Identifier) {
      set_error("expected parameter name");
      return nullptr;
    }
    param.name = cur_tok.text;
    next_token();

    if (cur_tok.type != TokenType::Colon) {
      set_error("expected ':' after parameter name");
      return nullptr;
    }
    next_token();

    param.type_ann = parse_type_annotation();
    if (has_error) return nullptr;
    params.push_back(std::move(param));
  }
  next_token(); // consume ')'

  if (cur_tok.type != TokenType::Arrow) {
    set_error("expected '->' and return type");
    return nullptr;
  }
  next_token();

  TypeAnnotation return_type = parse_type_annotation();
  if (has_error) return nullptr;

  skip_newlines();
  auto body = parse_block();
  if (has_error) return nullptr;

  auto decl = std::make_unique<FnDecl>();
  decl->name = name;
  decl->params = std::move(params);
  decl->return_type = return_type;
  decl->body = std::move(body);
  return decl;
}

// `extern fn name(params, ...) -> T` declares a foreign symbol (typically
// from a C library) to link against. It has no body — codegen emits an
// LLVM function declaration only, never a definition — and its parameter
// list may end in a bare `...` to mark it variadic (e.g. printf).
std::unique_ptr<FnDecl> Parser::parse_extern_fn_decl() {
  next_token(); // consume 'extern'

  if (cur_tok.type != TokenType::Fn) {
    set_error("expected 'fn' after 'extern'");
    return nullptr;
  }
  next_token(); // consume 'fn'

  if (cur_tok.type != TokenType::Identifier) {
    set_error("expected function name");
    return nullptr;
  }
  std::string name = cur_tok.text;
  next_token();

  if (cur_tok.type != TokenType::LParen) {
    set_error("expected '(' after function name");
    return nullptr;
  }
  next_token();

  std::vector<Param> params;
  bool is_variadic = false;
  while (cur_tok.type != TokenType::RParen) {
    if (!params.empty()) {
      if (cur_tok.type != TokenType::Comma) {
        set_error("expected ',' or ')' in parameter list");
        return nullptr;
      }
      next_token();
    }
    if (cur_tok.type == TokenType::Ellipsis) {
      next_token(); // consume '...'
      is_variadic = true;
      break; // '...' must be the last thing in the parameter list
    }
    Param param;
    if (cur_tok.type != TokenType::Identifier) {
      set_error("expected parameter name or '...'");
      return nullptr;
    }
    param.name = cur_tok.text;
    next_token();

    if (cur_tok.type != TokenType::Colon) {
      set_error("expected ':' after parameter name");
      return nullptr;
    }
    next_token();

    param.type_ann = parse_type_annotation();
    if (has_error) return nullptr;
    params.push_back(std::move(param));
  }

  if (cur_tok.type != TokenType::RParen) {
    set_error("expected ')' after '...'");
    return nullptr;
  }
  next_token(); // consume ')'

  if (cur_tok.type != TokenType::Arrow) {
    set_error("expected '->' and return type");
    return nullptr;
  }
  next_token();

  TypeAnnotation return_type = parse_type_annotation();
  if (has_error) return nullptr;

  auto decl = std::make_unique<FnDecl>();
  decl->name = name;
  decl->params = std::move(params);
  decl->return_type = return_type;
  decl->is_extern = true;
  decl->is_variadic = is_variadic;
  return decl;
}

std::vector<std::unique_ptr<Stmt>> Parser::parse_block() {
  if (cur_tok.type != TokenType::LBrace) {
    set_error("expected '{'");
    return {};
  }
  next_token(); // consume '{'
  skip_newlines();

  std::vector<std::unique_ptr<Stmt>> stmts;
  while (cur_tok.type != TokenType::RBrace && cur_tok.type != TokenType::Eof) {
    auto stmt = parse_stmt();
    if (!stmt) break;
    stmts.push_back(std::move(stmt));
    skip_newlines();
  }
  if (has_error) return {};

  if (cur_tok.type != TokenType::RBrace) {
    set_error("expected '}'");
    return {};
  }
  next_token(); // consume '}'
  return stmts;
}

std::unique_ptr<Stmt> Parser::parse_stmt() {
  if (cur_tok.type == TokenType::Let) {
    return parse_let_stmt();
  }
  if (cur_tok.type == TokenType::Return) {
    return parse_return_stmt();
  }
  if (cur_tok.type == TokenType::If) {
    return parse_if_stmt();
  }
  if (cur_tok.type == TokenType::For) {
    return parse_for_stmt();
  }
  auto expr = parse_expr();
  if (!expr) return nullptr;
  return std::make_unique<ExprStmt>(std::move(expr));
}

std::unique_ptr<LetStmt> Parser::parse_let_stmt() {
  next_token(); // consume 'let'

  TypeAnnotation type_ann;
  std::string name;
  std::unique_ptr<Expr> init;
  if (!parse_let_common(type_ann, name, init)) return nullptr;

  auto stmt = std::make_unique<LetStmt>();
  stmt->type_ann = type_ann;
  stmt->name = name;
  stmt->init_expr = std::move(init);
  return stmt;
}

std::unique_ptr<ReturnStmt> Parser::parse_return_stmt() {
  next_token(); // consume 'return'

  if (cur_tok.type == TokenType::Number ||
      cur_tok.type == TokenType::True ||
      cur_tok.type == TokenType::False ||
      cur_tok.type == TokenType::StringLiteral ||
      cur_tok.type == TokenType::Identifier ||
      cur_tok.type == TokenType::Asm ||
      cur_tok.type == TokenType::Match ||
      cur_tok.type == TokenType::Enum ||
      cur_tok.type == TokenType::LParen ||
      cur_tok.type == TokenType::LSquare ||
      cur_tok.type == TokenType::Minus ||
      cur_tok.type == TokenType::BitNot ||
      cur_tok.type == TokenType::Star ||
      cur_tok.type == TokenType::Ampersand ||
      cur_tok.type == TokenType::Null) {
    auto expr = parse_expr();
    if (!expr) return nullptr;
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->value = std::move(expr);
    return stmt;
  }

  return std::make_unique<ReturnStmt>(); // bare return (void)
}

std::unique_ptr<IfStmt> Parser::parse_if_stmt() {
  next_token(); // consume 'if'

  auto cond = parse_expr();
  if (!cond) return nullptr;

  skip_newlines();
  auto then_branch = parse_block();
  if (has_error) return nullptr;

  skip_newlines();
  std::vector<std::unique_ptr<Stmt>> else_branch;
  if (cur_tok.type == TokenType::Else) {
    next_token(); // consume 'else'
    skip_newlines();
    if (cur_tok.type == TokenType::If) {
      // `else if ...` chains: parse the nested if as a single statement
      // rather than requiring a `{ }` block. gen_if_stmt() runs
      // else_branch through gen_stmt(), which dynamic_casts back to
      // IfStmt and recurses, so no codegen changes are needed.
      auto elseif_stmt = parse_if_stmt();
      if (!elseif_stmt) return nullptr;
      else_branch.push_back(std::move(elseif_stmt));
    } else {
      else_branch = parse_block();
      if (has_error) return nullptr;
    }
  }

  auto stmt = std::make_unique<IfStmt>();
  stmt->condition = std::move(cond);
  stmt->then_branch = std::move(then_branch);
  stmt->else_branch = std::move(else_branch);
  return stmt;
}

std::unique_ptr<ForStmt> Parser::parse_for_stmt() {
  next_token(); // consume 'for'
  skip_newlines();

  std::unique_ptr<Stmt> init;
  if (cur_tok.type == TokenType::Let) {
    init = parse_let_stmt();
    if (!init) return nullptr;
  } else if (cur_tok.type != TokenType::Semicolon) {
    auto expr = parse_expr();
    if (!expr) return nullptr;
    init = std::make_unique<ExprStmt>(std::move(expr));
  }

  if (cur_tok.type != TokenType::Semicolon) {
    set_error("expected ';' after for init");
    return nullptr;
  }
  next_token();
  skip_newlines();

  std::unique_ptr<Expr> cond;
  if (cur_tok.type != TokenType::Semicolon) {
    cond = parse_expr();
    if (!cond) return nullptr;
  }

  if (cur_tok.type != TokenType::Semicolon) {
    set_error("expected ';' after for condition");
    return nullptr;
  }
  next_token();
  skip_newlines();

  std::unique_ptr<Expr> update;
  if (cur_tok.type != TokenType::LBrace) {
    update = parse_expr();
    if (!update) return nullptr;
  }

  skip_newlines();
  auto body = parse_block();
  if (has_error) return nullptr;

  auto stmt = std::make_unique<ForStmt>();
  stmt->init = std::move(init);
  stmt->condition = std::move(cond);
  stmt->update = std::move(update);
  stmt->body = std::move(body);
  return stmt;
}

// -------------------------------------------------------------------------
// Expressions (recursive descent, operator precedence)
// -------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parse_expr() {
  return parse_assignment();
}

std::unique_ptr<Expr> Parser::parse_assignment() {
  auto left = parse_logical_or();
  if (!left) return nullptr;

  auto is_lvalue = [](Expr *e) {
    return dynamic_cast<IdentExpr *>(e) ||
           dynamic_cast<DerefExpr *>(e) ||
           dynamic_cast<SubscriptExpr *>(e) ||
           dynamic_cast<FieldAccessExpr *>(e);
  };

  BinOp compound_op;
  bool is_compound = false;
  if (cur_tok.type == TokenType::PlusEq) { compound_op = BinOp::Add; is_compound = true; }
  else if (cur_tok.type == TokenType::MinusEq) { compound_op = BinOp::Sub; is_compound = true; }
  else if (cur_tok.type == TokenType::StarEq) { compound_op = BinOp::Mul; is_compound = true; }
  else if (cur_tok.type == TokenType::SlashEq) { compound_op = BinOp::Div; is_compound = true; }
  else if (cur_tok.type == TokenType::AndEq) { compound_op = BinOp::BitAnd; is_compound = true; }
  else if (cur_tok.type == TokenType::OrEq) { compound_op = BinOp::BitOr; is_compound = true; }
  else if (cur_tok.type == TokenType::XorEq) { compound_op = BinOp::Xor; is_compound = true; }
  else if (cur_tok.type == TokenType::ShlEq) { compound_op = BinOp::Shl; is_compound = true; }
  else if (cur_tok.type == TokenType::ShrEq) { compound_op = BinOp::Shr; is_compound = true; }

  if (is_compound) {
    if (!is_lvalue(left.get())) {
      set_error("left side of compound assignment must be a variable, dereference, subscript, or field access");
      return nullptr;
    }
    next_token(); // consume the compound operator
    auto value = parse_assignment();
    if (!value) return nullptr;
    return std::make_unique<CompoundAssignExpr>(std::move(left), compound_op, std::move(value));
  }

  if (cur_tok.type == TokenType::Equals) {
    if (!is_lvalue(left.get())) {
      set_error("left side of assignment must be a variable, dereference, subscript, or field access");
      return nullptr;
    }
    next_token(); // consume '='
    auto value = parse_assignment(); // right-associative
    if (!value) return nullptr;
    return std::make_unique<AssignExpr>(std::move(left), std::move(value));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_logical_or() {
  auto left = parse_logical_and();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::OrOr) {
    next_token();
    auto right = parse_logical_and();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), BinOp::Or, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_logical_and() {
  auto left = parse_bitwise_or();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::AndAnd) {
    next_token();
    auto right = parse_bitwise_or();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), BinOp::And, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_bitwise_or() {
  auto left = parse_bitwise_xor();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::BitOr) {
    next_token();
    auto right = parse_bitwise_xor();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), BinOp::BitOr, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_bitwise_xor() {
  auto left = parse_bitwise_and();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::Xor) {
    next_token();
    auto right = parse_bitwise_and();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), BinOp::Xor, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_bitwise_and() {
  auto left = parse_comparison();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::Ampersand) {
    next_token();
    auto right = parse_comparison();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), BinOp::BitAnd, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_comparison() {
  auto left = parse_shift();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::Eq ||
         cur_tok.type == TokenType::Ne ||
         cur_tok.type == TokenType::Less ||
         cur_tok.type == TokenType::Greater ||
         cur_tok.type == TokenType::LessEqual ||
         cur_tok.type == TokenType::GreaterEqual) {
    BinOp op;
    switch (cur_tok.type) {
      case TokenType::Eq:          op = BinOp::Eq; break;
      case TokenType::Ne:          op = BinOp::Ne; break;
      case TokenType::Less:        op = BinOp::Less; break;
      case TokenType::Greater:     op = BinOp::Greater; break;
      case TokenType::LessEqual:   op = BinOp::Le; break;
      case TokenType::GreaterEqual: op = BinOp::Ge; break;
      default: return left;
    }
    next_token();
    auto right = parse_shift();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_shift() {
  auto left = parse_additive();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::Shr || cur_tok.type == TokenType::Shl) {
    BinOp op = (cur_tok.type == TokenType::Shr) ? BinOp::Shr : BinOp::Shl;
    next_token();
    auto right = parse_additive();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_additive() {
  auto left = parse_multiplicative();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::Plus || cur_tok.type == TokenType::Minus) {
    BinOp op = (cur_tok.type == TokenType::Plus) ? BinOp::Add : BinOp::Sub;
    next_token();
    auto right = parse_multiplicative();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_multiplicative() {
  auto left = parse_unary();
  if (!left) return nullptr;

  while (cur_tok.type == TokenType::Star || cur_tok.type == TokenType::Slash) {
    BinOp op = (cur_tok.type == TokenType::Star) ? BinOp::Mul : BinOp::Div;
    next_token();
    auto right = parse_unary();
    if (!right) return nullptr;
    left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_unary() {
  if (cur_tok.type == TokenType::Minus) {
    next_token();
    auto operand = parse_unary();
    if (!operand) return nullptr;
    return std::make_unique<UnaryExpr>(UnaryOp::Neg, std::move(operand));
  }
  if (cur_tok.type == TokenType::BitNot) {
    next_token();
    auto operand = parse_unary();
    if (!operand) return nullptr;
    return std::make_unique<UnaryExpr>(UnaryOp::BitNot, std::move(operand));
  }
  if (cur_tok.type == TokenType::Star) {
    next_token();
    auto operand = parse_unary();
    if (!operand) return nullptr;
    return std::make_unique<DerefExpr>(std::move(operand));
  }
  auto prim = parse_primary();
  if (!prim) return nullptr;
  return parse_postfix(std::move(prim));
}

std::unique_ptr<Expr> Parser::parse_postfix(std::unique_ptr<Expr> left) {
  // Handle subscript access: arr[i]
  while (cur_tok.type == TokenType::LSquare) {
    next_token(); // consume '['
    auto index = parse_expr();
    if (!index) return nullptr;
    if (cur_tok.type != TokenType::RSquare) {
      set_error("expected ']' after index expression");
      return nullptr;
    }
    next_token(); // consume ']'
    left = std::make_unique<SubscriptExpr>(std::move(left), std::move(index));
  }
  // Handle field access: obj.field
  while (cur_tok.type == TokenType::Dot) {
    next_token(); // consume '.'
    if (cur_tok.type != TokenType::Identifier) {
      set_error("expected field name after '.'");
      return nullptr;
    }
    std::string field = cur_tok.text;
    next_token();
    left = std::make_unique<FieldAccessExpr>(std::move(left), field);
  }
  // Handle constructor expression: VariantName { field: expr, ... }
  if (cur_tok.type == TokenType::LBrace) {
    auto *ident = dynamic_cast<IdentExpr *>(left.get());
    if (ident && (known_variants.count(ident->name) > 0 || known_structs.count(ident->name) > 0)) {
      std::string variant_name = ident->name;
      next_token(); // consume '{'
      skip_newlines();
      auto ctor = std::make_unique<ConstructorExpr>();
      ctor->variant_name = variant_name;
      while (cur_tok.type != TokenType::RBrace && cur_tok.type != TokenType::Eof) {
        if (cur_tok.type != TokenType::Identifier) {
          set_error("expected field name in constructor");
          return nullptr;
        }
        std::string field_name = cur_tok.text;
        next_token();
        skip_newlines();
        if (cur_tok.type != TokenType::Colon) {
          set_error("expected ':' after field name in constructor");
          return nullptr;
        }
        next_token();
        skip_newlines();
        auto field_val = parse_expr();
        if (!field_val) return nullptr;
        ctor->fields.push_back({field_name, std::move(field_val)});
        skip_newlines();
        if (cur_tok.type == TokenType::Comma) {
          next_token();
          skip_newlines();
        }
      }
      if (cur_tok.type != TokenType::RBrace) {
        set_error("expected '}' to close constructor");
        return nullptr;
      }
      next_token(); // consume '}'
      return ctor;
    }
  }
  return left;
}

std::unique_ptr<Expr> Parser::parse_primary() {
  if (cur_tok.type == TokenType::True) {
    auto expr = std::make_unique<NumberExpr>(cur_tok.num_val);
    next_token();
    return expr;
  }
  if (cur_tok.type == TokenType::False) {
    auto expr = std::make_unique<NumberExpr>(cur_tok.num_val);
    next_token();
    return expr;
  }
  if (cur_tok.type == TokenType::Number) {
    auto expr = std::make_unique<NumberExpr>(cur_tok.num_val);
    next_token();
    return expr;
  }
  if (cur_tok.type == TokenType::StringLiteral) {
    auto expr = std::make_unique<StringExpr>(cur_tok.text);
    next_token();
    return expr;
  }
  if (cur_tok.type == TokenType::Null) {
    next_token();
    return std::make_unique<NullExpr>();
  }
  if (cur_tok.type == TokenType::Ampersand) {
    next_token();
    auto operand = parse_unary();
    if (!operand) return nullptr;
    return std::make_unique<AddressOfExpr>(std::move(operand));
  }
  if (cur_tok.type == TokenType::LSquare) {
    return parse_array_literal();
  }
  if (cur_tok.type == TokenType::Identifier) {
    std::string name = cur_tok.text;
    next_token();
    while (cur_tok.type == TokenType::ColonColon) {
      next_token(); // consume '::'
      if (cur_tok.type != TokenType::Identifier) {
        set_error("expected identifier after '::'");
        return nullptr;
      }
      name += "::" + cur_tok.text;
      next_token();
    }
    if (cur_tok.type == TokenType::LParen) {
      return parse_call_rest(name);
    }
    return std::make_unique<IdentExpr>(name);
  }
  if (cur_tok.type == TokenType::Asm) {
    next_token(); // consume 'asm'
    if (cur_tok.type != TokenType::LParen) {
      set_error("expected '(' after asm");
      return nullptr;
    }
    next_token(); // consume '('
    if (cur_tok.type != TokenType::StringLiteral) {
      set_error("expected string literal in asm");
      return nullptr;
    }
    auto expr = std::make_unique<AsmExpr>();
    expr->asm_code = cur_tok.text;
    next_token();
    if (cur_tok.type != TokenType::RParen) {
      set_error("expected ')'");
      return nullptr;
    }
    next_token(); // consume ')'
    return expr;
  }
  if (cur_tok.type == TokenType::LParen) {
    next_token();
    auto expr = parse_expr();
    if (cur_tok.type != TokenType::RParen) {
      set_error("expected ')'");
      return nullptr;
    }
    next_token();
    return expr;
  }
  if (cur_tok.type == TokenType::Match) {
    return parse_match_expr();
  }
  set_error("expected expression");
  return nullptr;
}

std::unique_ptr<Expr> Parser::parse_array_literal() {
  next_token(); // consume '['
  std::vector<std::unique_ptr<Expr>> elements;
  while (cur_tok.type != TokenType::RSquare) {
    if (!elements.empty()) {
      if (cur_tok.type != TokenType::Comma) {
        set_error("expected ',' or ']' in array literal");
        return nullptr;
      }
      next_token();
    }
    auto el = parse_expr();
    if (!el) return nullptr;
    elements.push_back(std::move(el));
  }
  next_token(); // consume ']'
  auto expr = std::make_unique<ArrayLitExpr>();
  expr->elements = std::move(elements);
  return expr;
}

std::unique_ptr<Expr> Parser::parse_call_rest(const std::string &name) {
  next_token(); // consume '('

  std::vector<std::unique_ptr<Expr>> args;
  while (cur_tok.type != TokenType::RParen) {
    if (!args.empty()) {
      if (cur_tok.type != TokenType::Comma) {
        set_error("expected ',' or ')' in arguments");
        return nullptr;
      }
      next_token();
    }
    auto arg = parse_expr();
    if (!arg) return nullptr;
    args.push_back(std::move(arg));
  }
  next_token(); // consume ')'

  auto expr = std::make_unique<CallExpr>();
  expr->callee = name;
  expr->args = std::move(args);
  return expr;
}

std::unique_ptr<Expr> Parser::parse_match_expr() {
  next_token(); // consume 'match'
  auto value = parse_expr();
  if (!value) return nullptr;
  skip_newlines();
  if (cur_tok.type != TokenType::LBrace) {
    set_error("expected '{' after match expression");
    return nullptr;
  }
  next_token(); // consume '{'
  skip_newlines();

  auto mexpr = std::make_unique<MatchExpr>();
  mexpr->value = std::move(value);

  while (cur_tok.type != TokenType::RBrace && cur_tok.type != TokenType::Eof) {
    auto pattern = parse_pattern();
    if (!pattern) return nullptr;
    skip_newlines();
    if (cur_tok.type != TokenType::FatArrow) {
      set_error("expected '=>' after pattern in match arm");
      return nullptr;
    }
    next_token(); // consume '=>'
    skip_newlines();
    auto body = parse_expr();
    if (!body) return nullptr;
    MatchArm arm;
    arm.pattern = std::move(pattern);
    arm.expr = std::move(body);
    mexpr->arms.push_back(std::move(arm));
    skip_newlines();
    // Optional comma separator between arms
    if (cur_tok.type == TokenType::Comma) {
      next_token();
      skip_newlines();
    }
  }
  if (cur_tok.type != TokenType::RBrace) {
    set_error("expected '}' to close match");
    return nullptr;
  }
  next_token(); // consume '}'
  return mexpr;
}

std::unique_ptr<Pattern> Parser::parse_pattern() {
  if (cur_tok.type == TokenType::Identifier) {
    std::string name = cur_tok.text;
    next_token();
    // Wildcard
    if (name == "_")
      return std::make_unique<WildcardPattern>();
    // Struct pattern: Identifier { ... }
    skip_newlines();
    if (cur_tok.type == TokenType::LBrace) {
      next_token(); // consume '{'
      skip_newlines();
      auto pat = std::make_unique<StructPattern>();
      pat->struct_name = name;
      while (cur_tok.type != TokenType::RBrace && cur_tok.type != TokenType::Eof) {
        if (cur_tok.type != TokenType::Identifier) {
          set_error("expected field name in struct pattern");
          return nullptr;
        }
        std::string field_name = cur_tok.text;
        next_token();
        skip_newlines();
        std::unique_ptr<Pattern> field_pat;
        if (cur_tok.type == TokenType::Colon) {
          next_token(); // consume ':'
          skip_newlines();
          field_pat = parse_pattern();
          if (!field_pat) return nullptr;
        } else {
          // Shorthand: field_name acts as both the field name and variable binding
          field_pat = std::make_unique<VariablePattern>(field_name);
        }
        pat->fields.push_back({field_name, std::move(field_pat)});
        skip_newlines();
        if (cur_tok.type == TokenType::Comma) {
          next_token();
          skip_newlines();
        }
      }
      if (cur_tok.type != TokenType::RBrace) {
        set_error("expected '}' to close struct pattern");
        return nullptr;
      }
      next_token(); // consume '}'
      return pat;
    }
    // Variable pattern
    return std::make_unique<VariablePattern>(name);
  }
  if (cur_tok.type == TokenType::Number ||
      cur_tok.type == TokenType::True ||
      cur_tok.type == TokenType::False) {
    auto expr = std::make_unique<NumberExpr>(cur_tok.num_val);
    next_token();
    return std::make_unique<LiteralPattern>(std::move(expr));
  }
  if (cur_tok.type == TokenType::StringLiteral) {
    auto expr = std::make_unique<StringExpr>(cur_tok.text);
    next_token();
    return std::make_unique<LiteralPattern>(std::move(expr));
  }
  if (cur_tok.type == TokenType::Null) {
    next_token();
    return std::make_unique<LiteralPattern>(std::make_unique<NullExpr>());
  }
  if (cur_tok.type == TokenType::Minus) {
    next_token();
    if (cur_tok.type != TokenType::Number) {
      set_error("expected number after '-' in pattern");
      return nullptr;
    }
    auto expr = std::make_unique<NumberExpr>(-cur_tok.num_val);
    next_token();
    return std::make_unique<LiteralPattern>(std::move(expr));
  }
  set_error("expected pattern");
  return nullptr;
}