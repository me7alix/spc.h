#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#define SPC_IMPLEMENTATION
#include "spc.h"

typedef struct AST {
	enum {
		AST_NUMBER,
		AST_BIN_EXPR,
	} kind;

	union {
		struct {
			enum {
				AST_OP_ADD,
				AST_OP_SUB,
				AST_OP_MUL,
				AST_OP_DIV,
			} op;

			struct AST *lhs;
			struct AST *rhs;
		} bin_expr;

		double number;
	} as;
} AST;

AST *number(double num) {
	AST *n = malloc(sizeof(*n));
	n->kind = AST_NUMBER;
	n->as.number = num;
}


AST *bin_expr(int op) {
	AST *n = malloc(sizeof(*n));
	n->kind = AST_BIN_EXPR;
	n->as.bin_expr.op = op;
	n->as.bin_expr.lhs = NULL;
	n->as.bin_expr.rhs = NULL;
}

void ast_dump(AST *n) {
	switch (n->kind) {
	case AST_NUMBER:
		printf("%g", n->as.number);
		break;

	case AST_BIN_EXPR:
		printf("(");
		ast_dump(n->as.bin_expr.lhs);

		switch (n->as.bin_expr.op) {
			case AST_OP_ADD: printf(" + "); break;
			case AST_OP_SUB: printf(" - "); break;
			case AST_OP_MUL: printf(" * "); break;
			case AST_OP_DIV: printf(" / "); break;
		}

		ast_dump(n->as.bin_expr.rhs);
		printf(")");
	}
}

double ast_calc(AST *n) {
	switch (n->kind) {
	case AST_NUMBER:
		return n->as.number;

	case AST_BIN_EXPR:
		double lhs = ast_calc(n->as.bin_expr.lhs);
		double rhs = ast_calc(n->as.bin_expr.rhs);

		switch (n->as.bin_expr.op) {
			case AST_OP_ADD: return lhs + rhs;
			case AST_OP_SUB: return lhs - rhs;
			case AST_OP_MUL: return lhs * rhs;
			case AST_OP_DIV: return lhs / rhs;
		}
	}
}

typedef struct {
	char *stream;
} Scanner;

static char scn_peek(Scanner *l) { return *(l->stream); }
static char scn_next(Scanner *l) { return *(l->stream++); }
static void scn_skip_ws(Scanner *l) { while (scn_peek(l) == ' ') scn_next(l); }

SPC_Result parse_number_f(SPC_Parser *p, Input *inp) {
	Scanner *l = inp;
	scn_skip_ws(l);

	if (isdigit(scn_peek(l)) || scn_peek(l) == '-') {
		char digits[32] = {0};
		size_t count = 0;
		bool met_dot = false;
		int z = 1;

		if (scn_peek(l) == '-') {
			z = -1;
			scn_next(l);
		}

		while (isdigit(scn_peek(l)) || scn_peek(l) == '.') {
			if (scn_peek(l) == '.') {
				if (met_dot) return spc_error("incorrect number");
				met_dot = true;
			}

			digits[count++] = scn_next(l);
		}

		double num = z * atof(digits);
		return spc_success(number(num));
	}

	return spc_error("incorrect number");
}

SPC_Parser *parse_number() {
	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = parse_number_f;
	return p;
}

typedef struct {
	int kind;
} ParseOperCtx;

SPC_Result parse_operator_f(SPC_Parser *p, Input *inp) {
	ParseOperCtx *ctx = p->ctx;
	Scanner *l = inp;
	scn_skip_ws(l);

	switch (scn_peek(l)) {
	case '+':
		if (ctx->kind == AST_OP_ADD) {
			scn_next(l);
			return spc_success(bin_expr(AST_OP_ADD));
		} break;
	case '-':
		if (ctx->kind == AST_OP_SUB) {
			scn_next(l);
			return spc_success(bin_expr(AST_OP_SUB));
		} break;
	case '*':
		if (ctx->kind == AST_OP_MUL) {
			scn_next(l);
			return spc_success(bin_expr(AST_OP_MUL));
		} break;
	case '/':
		if (ctx->kind == AST_OP_DIV) {
			scn_next(l);
			return spc_success(bin_expr(AST_OP_DIV));
		}
	}

	return spc_error("incorrect operator");
}

SPC_Parser *parse_operator(int kind) {
	SPC_Parser *p = malloc(sizeof(*p));
	ParseOperCtx *ctx = malloc(sizeof(*ctx));
	ctx->kind = kind;
	p->parse = parse_operator_f;
	p->ctx = ctx;
	return p;
}

typedef struct {
	char character;
} ParseCharCtx;

SPC_Result parse_char_f(SPC_Parser *p, Input *inp) {
	ParseCharCtx *ctx = p->ctx;
	Scanner *l = inp;
	scn_skip_ws(l);

	if (ctx->character == scn_peek(l)) {
		scn_next(l);
		return spc_success(NULL);
	}

	return spc_error("wrong character");
}


SPC_Parser *parse_char(char ch) {
	SPC_Parser *p = malloc(sizeof(*p));
	ParseCharCtx *ctx = malloc(sizeof(*ctx));
	ctx->character = ch;
	p->parse = parse_char_f;
	p->ctx = ctx;
	return p;
}

void *comb(void *lhs, void *e, void *rhs) {
	((AST*)e)->as.bin_expr.lhs = lhs;
	((AST*)e)->as.bin_expr.rhs = rhs;
	return e;
}

typedef struct {
	SPC_Parser *parser;
} TryCtx;

SPC_Result try_f(SPC_Parser *p, Input *inp) {
	TryCtx *ctx = p->ctx;
	Scanner saved = *(Scanner*)inp;
	SPC_Result res = ctx->parser->parse(ctx->parser, inp);
	if (!res.success) *(Scanner*)inp = saved;
	return res;
}

// Saves the scanner state and restores it if parsing fails
SPC_Parser *try(SPC_Parser *parser) {
	SPC_Parser *p = malloc(sizeof(*p));
	TryCtx *ctx = malloc(sizeof(*ctx));
	ctx->parser = parser;
	p->ctx = ctx;
	p->parse = try_f;
	return p;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "provide the expression");
		return 1;
	}

	Scanner scanner = {argv[1]};

	SPC_Parser *expr = NULL;
	SPC_Parser *lazy_expr = spc_lazy(&expr);

	SPC_Parser *add = parse_operator(AST_OP_ADD);
	SPC_Parser *sub = parse_operator(AST_OP_SUB);
	SPC_Parser *mul = parse_operator(AST_OP_MUL);
	SPC_Parser *div = parse_operator(AST_OP_DIV);
	SPC_Parser *num = parse_number();

	SPC_Parser *par     = try(spc_between(parse_char('('), lazy_expr, parse_char(')')));
	SPC_Parser *value   = try(spc_choice(num, par));
	SPC_Parser *product = try(spc_chain_left(value,   spc_choice(mul, div), comb));
	            expr    = try(spc_chain_left(product, spc_choice(add, sub), comb));

	SPC_Result res = expr->parse(expr, &scanner);

	if (scn_peek(&scanner) != '\0') {
		res = spc_error("incorrect expression");
	}

	if (res.success) {
		double calc = ast_calc(res.data);
		ast_dump(res.data);
		printf(" = %g\n", calc);
	} else {
		printf("%s\n", res.error);
	}

	return 0;
}
