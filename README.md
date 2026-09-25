# spc.h

**spc.h** is a simple parser combinator library written in C without any external dependencies.

## Idea

The idea behind **spc.h** is to provide a simple parser combinator that is abstracted away from the lexer.

## Demo

To test my library, I decided to implement a math expression parser.

The parser uses the following grammar:

```
expr    ::= <product> { ('+' | '-') <product> }
value   ::= <number> | '(' <expr> ')'
product ::= <value> { ('*' | '/') <value> }
```
