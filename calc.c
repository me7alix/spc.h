#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#define SPC_IMPLEMENTATION
#include "spc.h"

/* AST */

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
	return n;
}

AST *bin_expr(int op) {
	AST *n = malloc(sizeof(*n));
	n->kind = AST_BIN_EXPR;
	n->as.bin_expr.op = op;
	n->as.bin_expr.lhs = NULL;
	n->as.bin_expr.rhs = NULL;
	return n;
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

	case AST_BIN_EXPR:;
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

/* Scanner */

typedef struct {
	char *stream;
} Scanner;

#define peek(l) (*((l)->stream))
#define next(l) (*((l)->stream++))
#define skip_ws(l) while (peek(l) == ' ') next(l)

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

/* Parsers */

SPC_Result pnum_f(SPC_Parser *p, SPC_Input *inp) {
	Scanner *l = inp->get(inp->self);
	skip_ws(l);

	if (isdigit(peek(l)) || peek(l) == '-') {
		char digits[32] = {0};
		size_t count = 0;
		bool met_dot = false;
		int z = 1;

		if (peek(l) == '-') {
			z = -1;
			next(l);
		}

		while (isdigit(peek(l)) || peek(l) == '.') {
			if (peek(l) == '.') {
				if (met_dot) return spc_error("incorrect number", NULL);
				met_dot = true;
			}

			digits[count++] = next(l);
		}

		double num = z * atof(digits);
		return spc_success(number(num), free);
	}

	return spc_error("incorrect number", NULL);
}

SPC_Parser *pnum() {
	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = pnum_f;
	return p;
}

void *map_bin_op(void *data) {
	switch ((char)data) {
		case '+': return bin_expr(AST_OP_ADD);
		case '-': return bin_expr(AST_OP_SUB);
		case '*': return bin_expr(AST_OP_MUL);
		case '/': return bin_expr(AST_OP_DIV);
	}
}

typedef struct {
	char character;
} ParseCharCtx;

SPC_Result pch_f(SPC_Parser *p, SPC_Input *inp) {
	ParseCharCtx *ctx = p->ctx;
	Scanner *l = inp->get(inp->self);

	skip_ws(l);

	if (ctx->character == peek(l)) {
		next(l);
		return spc_success((void*)ctx->character, NULL);
	}

	return spc_error("wrong character", NULL);
}


SPC_Parser *pch(char ch) {
	SPC_Parser *p = malloc(sizeof(*p));
	ParseCharCtx *ctx = malloc(sizeof(*ctx));
	ctx->character = ch;
	p->parse = pch_f;
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
		fprintf(stderr, "provide an expression\n");
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

	SPC_Combine3 cbo = {
		bin_expr_comb,
		free,
	};

	SPC_Map mbo = {
		map_bin_op,
		free
	};

	SPC_Parser *expr = NULL;
	SPC_Parser *lazy_expr = spc_lazy(&expr);

	SPC_Parser *add = spc_map(pch('+'), mbo);
	SPC_Parser *sub = spc_map(pch('-'), mbo);
	SPC_Parser *mul = spc_map(pch('*'), mbo);
	SPC_Parser *div = spc_map(pch('/'), mbo);

	SPC_Parser *par     = spc_between(pch('('), lazy_expr, pch(')'));
	SPC_Parser *value   = spc_choice(pnum(), par);
	SPC_Parser *product = spc_chain_left(value,   spc_choice(mul, div), cbo);
	            expr    = spc_chain_left(product, spc_choice(add, sub), cbo);

	SPC_Result res = expr->parse(expr, &inp);

	Scanner *scn = scn_mng_get(&scn_mng);
	if (peek(scn) != '\0') {
		res = spc_error("invalid expression", NULL);
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
