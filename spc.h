#ifndef SPC_H_
#define SPC_H_

#include <stdlib.h>
#include <string.h>

typedef struct {
	bool success;
	void *error;
	void *data;
} SPC_Result;

typedef void Input;
typedef struct SPC_Parser SPC_Parser;

typedef SPC_Result (*ParseFn)(SPC_Parser*, Input*);
typedef void *(*SPC_Combine2Fn)(void*, void*);
typedef void *(*SPC_Combine3Fn)(void*, void*, void*);
typedef void *(*SPC_MapFn)(void*);

struct SPC_Parser {
    ParseFn parse;
    void *ctx;
};

static SPC_Result spc_success(void *data) {
	return (SPC_Result){true, NULL, data};
}

static SPC_Result spc_error(void *error) {
	return (SPC_Result){false, error, NULL};
}

SPC_Parser *spc_map(SPC_Parser *parser, SPC_MapFn transform);
SPC_Parser *spc_choice(SPC_Parser *left, SPC_Parser *right);
SPC_Parser *spc_sequence(SPC_Parser *left, SPC_Parser *right, SPC_Combine2Fn combine);
SPC_Parser *spc_keep_right(SPC_Parser *left, SPC_Parser *right);
SPC_Parser *spc_keep_left(SPC_Parser *left, SPC_Parser *right);
SPC_Parser *spc_between(SPC_Parser *left, SPC_Parser *middle, SPC_Parser *right);
SPC_Parser *spc_lazy(SPC_Parser **ref);
SPC_Parser *spc_chain_left(SPC_Parser *operand, SPC_Parser *operator, SPC_Combine3Fn combine);
SPC_Parser *spc_chain_right(SPC_Parser *operand, SPC_Parser *operator, SPC_Combine3Fn combine);

#endif // SPC_H_

#ifdef SPC_IMPLEMENTATION

typedef struct {
	SPC_Parser *parser;
	SPC_MapFn transform;
} SPC_MapCtx;

SPC_Result spc_parse_map(SPC_Parser *self, Input *inp) {
	SPC_MapCtx *ctx = self->ctx;

	SPC_Result res = ctx->parser->parse(ctx->parser, inp);
	if (!res.success) return res;

	return spc_success(ctx->transform(res.data));
}

SPC_Parser *spc_map(SPC_Parser *parser, SPC_MapFn transform) {
	SPC_MapCtx *ctx = malloc(sizeof(*ctx));
	ctx->parser = parser;
	ctx->transform = transform;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_map;
	p->ctx = ctx;
	return p;
}

typedef struct {
    SPC_Parser *left;
    SPC_Parser *right;
} SPC_ChoiceCtx;

SPC_Result spc_parse_choice(SPC_Parser *self, Input *input) {
    SPC_ChoiceCtx *ctx = self->ctx;

    SPC_Result res = ctx->left->parse(ctx->left, input);
    if (res.success) {
        return res;
    }

    return ctx->right->parse(ctx->right, input);
}

SPC_Parser *spc_choice(SPC_Parser *left, SPC_Parser *right) {
    SPC_ChoiceCtx *ctx = malloc(sizeof(*ctx));
    ctx->left = left;
    ctx->right = right;

    SPC_Parser *p = malloc(sizeof(*p));
    p->parse = spc_parse_choice;
    p->ctx = ctx;
    return p;
}

typedef struct {
    SPC_Parser *left;
    SPC_Parser *right;
	SPC_Combine2Fn combine;
} SPC_SequenceCtx;

SPC_Result spc_parse_sequence(SPC_Parser *self, Input *input) {
    SPC_SequenceCtx *ctx = self->ctx;

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (!left.success) return left;

	SPC_Result right = ctx->right->parse(ctx->right, input);
    if (!right.success) return right;

    return spc_success(ctx->combine(left.data, right.data));
}

SPC_Parser *spc_sequence(SPC_Parser *left, SPC_Parser *right, SPC_Combine2Fn combine) {
    SPC_SequenceCtx *ctx = malloc(sizeof(*ctx));
    ctx->left = left;
    ctx->right = right;
	ctx->combine = combine;

    SPC_Parser *p = malloc(sizeof(*p));
    p->parse = spc_parse_sequence;
    p->ctx = ctx;
    return p;
}

typedef struct {
    SPC_Parser *left;
    SPC_Parser *right;
} SPC_KeepRightCtx;

SPC_Result parse_keep_right(SPC_Parser *self, Input *input) {
    SPC_KeepRightCtx *ctx = self->ctx;

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (!left.success) return left;

	SPC_Result right = ctx->right->parse(ctx->right, input);
    if (!right.success) return right;

    return spc_success(right.data);
}

SPC_Parser *spc_keep_right(SPC_Parser *left, SPC_Parser *right) {
    SPC_KeepRightCtx *ctx = malloc(sizeof(*ctx));
    ctx->left = left;
    ctx->right = right;

    SPC_Parser *p = malloc(sizeof(*p));
    p->parse = parse_keep_right;
    p->ctx = ctx;
    return p;
}

typedef struct {
    SPC_Parser *left;
    SPC_Parser *right;
} KeepLeftCtx;

SPC_Result spc_parse_keep_left(SPC_Parser *self, Input *input) {
    KeepLeftCtx *ctx = self->ctx;

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (!left.success) return left;

	SPC_Result right = ctx->right->parse(ctx->right, input);
    if (!right.success) return right;

    return spc_success(left.data);
}

SPC_Parser *spc_keep_left(SPC_Parser *left, SPC_Parser *right) {
    KeepLeftCtx *ctx = malloc(sizeof(*ctx));
    ctx->left = left;
    ctx->right = right;

    SPC_Parser *p = malloc(sizeof(*p));
    p->parse = spc_parse_keep_left;
    p->ctx = ctx;
    return p;
}

typedef struct {
    SPC_Parser *left;
    SPC_Parser *middle;
    SPC_Parser *right;
} SPC_BetweenCtx;

SPC_Result spc_parse_between(SPC_Parser *self, Input *input) {
    SPC_BetweenCtx *ctx = self->ctx;

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (!left.success) return left;

	SPC_Result middle = ctx->middle->parse(ctx->middle, input);
    if (!middle.success) return middle;

	SPC_Result right = ctx->right->parse(ctx->right, input);
    if (!right.success) return right;

    return spc_success(middle.data);
}

SPC_Parser *spc_between(SPC_Parser *left, SPC_Parser *middle, SPC_Parser *right) {
    SPC_BetweenCtx *ctx = malloc(sizeof(*ctx));
    ctx->left = left;
    ctx->middle = middle;
    ctx->right = right;

    SPC_Parser *p = malloc(sizeof(*p));
    p->parse = spc_parse_between;
    p->ctx = ctx;
    return p;
}

typedef struct {
	SPC_Parser **ref;
} SPC_LazyCtx;

SPC_Result spc_parse_lazy(SPC_Parser *self, Input *input) {
	SPC_LazyCtx *ctx = self->ctx;
	SPC_Parser *actual = *(ctx->ref);
	return actual->parse(actual, input);
}

SPC_Parser *spc_lazy(SPC_Parser **ref) {
	SPC_Parser *p = malloc(sizeof(*p));
	SPC_LazyCtx *ctx = malloc(sizeof(*ctx));
	ctx->ref = ref;

	p->parse = spc_parse_lazy;
	p->ctx = ctx;
	return p;
}

typedef struct {
	SPC_Parser *operand;
	SPC_Parser *operator;
	SPC_Combine3Fn combine;
} SPC_ChainLeftCtx;

SPC_Result spc_parse_chain_left(SPC_Parser *self, Input *inp) {
	SPC_ChainLeftCtx *ctx = self->ctx;
	SPC_Result left = ctx->operand->parse(ctx->operand, inp);
	if (!left.success) return left;

	while (true) {
		SPC_Result middle = ctx->operator->parse(ctx->operator, inp);
		if (!middle.success) break;

		SPC_Result right = ctx->operand->parse(ctx->operand, inp);
		if (!right.success) return right;

		left.data = ctx->combine(left.data, middle.data, right.data);
	}

	return left;
}

SPC_Parser *spc_chain_left(SPC_Parser *operand, SPC_Parser *operator, SPC_Combine3Fn combine) {
	SPC_ChainLeftCtx *ctx = malloc(sizeof(*ctx));
	ctx->operand = operand;
	ctx->operator = operator;
	ctx->combine = combine;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_chain_left;
	p->ctx = ctx;
	return p;
}

typedef struct {
	SPC_Parser *operand;
	SPC_Parser *operator;
	SPC_Combine3Fn combine;
} SPC_ChainRightCtx;

SPC_Result spc_parse_chain_right(SPC_Parser *self, Input *inp) {
	SPC_ChainRightCtx *ctx = self->ctx;

	SPC_Result left = ctx->operand->parse(ctx->operand, inp);
	if (!left.success) return left;

	SPC_Result middle = ctx->operator->parse(ctx->operator, inp);
	if (!middle.success) return left;

	SPC_Parser req_parser = {spc_parse_chain_right, ctx};
	SPC_Result right = req_parser.parse(&req_parser, inp);
	if (!right.success) return right;

	return spc_success(ctx->combine(left.data, middle.data, right.data));
}

SPC_Parser *spc_chain_right(SPC_Parser *operand, SPC_Parser *operator, SPC_Combine3Fn combine) {
	SPC_ChainRightCtx *ctx = malloc(sizeof(*ctx));
	ctx->operand = operand;
	ctx->operator = operator;
	ctx->combine = combine;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_chain_right;
	p->ctx = ctx;
	return p;
}

#endif // SPC_IMPLEMENTATION
