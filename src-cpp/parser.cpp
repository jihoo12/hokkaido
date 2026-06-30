#include "parser.h"

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
    } else {
      set_error("expected declaration (let, fn, struct)");
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
  } else if (cur_tok.type == TokenType::Float) {
    ann = {TypeKind::Float};
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
    // Struct type: the identifier is the struct name
    ann = {TypeKind::Struct};
    ann.struct_name = cur_tok.text;
    next_token();
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
  }

  if (cur_tok.type != TokenType::RBrace) {
    set_error("expected '}' after struct fields");
    return nullptr;
  }
  next_token(); // consume '}'

  auto decl = std::make_unique<StructDecl>();
  decl->name = name;
  decl->fields = std::move(fields);
  return decl;
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
      cur_tok.type == TokenType::LParen ||
      cur_tok.type == TokenType::LSquare ||
      cur_tok.type == TokenType::Minus ||
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
    else_branch = parse_block();
    if (has_error) return nullptr;
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
  } else if (cur_tok.type != TokenType::Comma) {
    auto expr = parse_expr();
    if (!expr) return nullptr;
    init = std::make_unique<ExprStmt>(std::move(expr));
  }

  if (cur_tok.type != TokenType::Comma) {
    set_error("expected ',' after for init");
    return nullptr;
  }
  next_token();
  skip_newlines();

  std::unique_ptr<Expr> cond;
  if (cur_tok.type != TokenType::Comma) {
    cond = parse_expr();
    if (!cond) return nullptr;
  }

  if (cur_tok.type != TokenType::Comma) {
    set_error("expected ',' after for condition");
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
  auto left = parse_comparison();
  if (!left) return nullptr;

  if (cur_tok.type == TokenType::Equals) {
    bool is_ident = dynamic_cast<IdentExpr *>(left.get()) != nullptr;
    bool is_deref = dynamic_cast<DerefExpr *>(left.get()) != nullptr;
    bool is_subscript = dynamic_cast<SubscriptExpr *>(left.get()) != nullptr;
    bool is_field = dynamic_cast<FieldAccessExpr *>(left.get()) != nullptr;
    if (!is_ident && !is_deref && !is_subscript && !is_field) {
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

std::unique_ptr<Expr> Parser::parse_comparison() {
  auto left = parse_additive();
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
    return std::make_unique<UnaryExpr>(std::move(operand));
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