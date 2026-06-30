#include "parser.h"

void Parser::next_token() {
  cur_tok = lexer.next_token();
  // Skip newlines (they're just separators, not significant)
  while (cur_tok.type == TOK_NEWLINE) {
    cur_tok = lexer.next_token();
  }
}

std::vector<std::unique_ptr<Decl>> Parser::parse_program() {
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

std::unique_ptr<LetDecl> Parser::parse_let_decl() {
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

std::unique_ptr<PrintDecl> Parser::parse_print_decl() {
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