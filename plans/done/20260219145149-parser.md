# Pratt Parser

**Status:** Done

Precedence climbing parser. `or` (prec 1) -> `and` (prec 2) -> `not` (prec 3, prefix) -> comparison (prec 4, non-assoc) -> `..` (prec 5, right) -> `+ -` (prec 6) -> `* / %` (prec 7) -> unary minus (prec 8) -> `**` (prec 9, right). Parenthesized grouping. `=` assignment and `my` declaration (prec 0, right).

AST nodes: NUMBER, STRING, IDENT, BOOL, NOTHING, BINOP, UNARY, DECL, ASSIGN, BLOCK, IF, CALL, WHILE, BREAK, CONTINUE. S-expr format output. `parser_is_complete()` drives continuation prompts and multiline accumulation.
