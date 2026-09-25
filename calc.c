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

static char scn_peek(Scanner *l) {
	return *(l->stream);
}

static char scn_next(Scanner *l) {
	return *(l->stream++);
}

static void scn_skip_ws(Scanner *l) {
	while (scn_peek(l) == ' ') scn_next(l);
}

typedef struct {
	Scanner stack[64];
	int depth;
} ScannerManager;

void *scn_mng_get(void *self) {
	ScannerManager *mng = self;
	return &mng->stack[mng->depth];
}

void scn_mng_mark(void *self) {
	ScannerManager *mng = self;
	mng->stack[mng->depth + 1] = mng->stack[mng->depth];
	mng->depth++;
}

void scn_mng_rewind(void *self) {
	ScannerManager *mng = self;
	mng->depth--;
}

void scn_mng_unmark(void *self) {
	ScannerManager *mng = self;
	mng->stack[mng->depth - 1] = mng->stack[mng->depth];
	mng->depth--;
}

SPC_Result parse_number_f(SPC_Parser *p, SPC_Input *inp) {
	Scanner *l = inp->get(inp->self);
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
				if (met_dot) return spc_error("incorrect number", NULL);
				met_dot = true;
			}

			digits[count++] = scn_next(l);
		}

		double num = z * atof(digits);
		return spc_success(number(num), free);
	}

	return spc_error("incorrect number", NULL);
}

SPC_Parser *parse_number() {
	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = parse_number_f;
	return p;
}

typedef struct {
	int kind;
} ParseOperCtx;

SPC_Result parse_operator_f(SPC_Parser *p, SPC_Input *inp) {
	ParseOperCtx *ctx = p->ctx;
	Scanner *l = inp->get(inp->self);
	scn_skip_ws(l);

	switch (scn_peek(l)) {
	case '+':
		if (ctx->kind == AST_OP_ADD) {
			scn_next(l);
			return spc_success(bin_expr(AST_OP_ADD), free);
		} break;
	case '-':
		if (ctx->kind == AST_OP_SUB) {
			scn_next(l);
			return spc_success(bin_expr(AST_OP_SUB), free);
		} break;
	case '*':
		if (ctx->kind == AST_OP_MUL) {
			scn_next(l);
			return spc_success(bin_expr(AST_OP_MUL), free);
		} break;
	case '/':
		if (ctx->kind == AST_OP_DIV) {
			scn_next(l);
			return spc_success(bin_expr(AST_OP_DIV), free);
		}
	}

	return spc_error("incorrect operator", NULL);
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

SPC_Result parse_char_f(SPC_Parser *p, SPC_Input *inp) {
	ParseCharCtx *ctx = p->ctx;
	Scanner *l = inp->get(inp->self);
	scn_skip_ws(l);

	if (ctx->character == scn_peek(l)) {
		scn_next(l);
		return spc_success(NULL, NULL);
	}

	return spc_error("wrong character", NULL);
}


SPC_Parser *parse_char(char ch) {
	SPC_Parser *p = malloc(sizeof(*p));
	ParseCharCtx *ctx = malloc(sizeof(*ctx));
	ctx->character = ch;
	p->parse = parse_char_f;
	p->ctx = ctx;
	return p;
}

void *bin_expr_comb(void *lhs, void *e, void *rhs) {
	((AST*)e)->as.bin_expr.lhs = lhs;
	((AST*)e)->as.bin_expr.rhs = rhs;
	return e;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "provide the expression");
		return 1;
	}

	ScannerManager scn_mng = {0};
	scn_mng.stack[0] = (Scanner){argv[1]};

	SPC_Input inp = {
		&scn_mng,
		scn_mng_get,
		scn_mng_mark,
		scn_mng_unmark,
		scn_mng_rewind,
	};

	SPC_Combine3 op = {
		bin_expr_comb,
		free,
	};

	SPC_Parser *expr = NULL;
	SPC_Parser *lazy_expr = spc_lazy(&expr);

	SPC_Parser *add = parse_operator(AST_OP_ADD);
	SPC_Parser *sub = parse_operator(AST_OP_SUB);
	SPC_Parser *mul = parse_operator(AST_OP_MUL);
	SPC_Parser *div = parse_operator(AST_OP_DIV);
	SPC_Parser *num = parse_number();

	SPC_Parser *par     = spc_between(parse_char('('), lazy_expr, parse_char(')'));
	SPC_Parser *value   = spc_choice(num, par);
	SPC_Parser *product = spc_chain_left(value,   spc_choice(mul, div), op);
	            expr    = spc_chain_left(product, spc_choice(add, sub), op);

	SPC_Result res = expr->parse(expr, &inp);

	Scanner *scn = scn_mng_get(&scn_mng);
	if (scn_peek(scn) != '\0') {
		res = spc_error("incorrect expression", NULL);
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
