#ifndef SPC_H_
#define SPC_H_

#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef void (*SPC_FreeFn)(void*);

typedef struct {
	bool success;
	void *error;
	void *data;
	SPC_FreeFn free_error;
	SPC_FreeFn free_data;
} SPC_Result;

typedef struct {
	void *self;
	void *(*get)(void*);
	void (*mark)(void*);
	void (*unmark)(void*);
	void (*rewind)(void*);
} SPC_Input;

typedef struct SPC_Parser SPC_Parser;
typedef SPC_Result (*ParseFn)(SPC_Parser*, SPC_Input*);
typedef void *(*SPC_Combine2Fn)(void*, void*);
typedef void *(*SPC_Combine3Fn)(void*, void*, void*);
typedef void *(*SPC_MapFn)(void*);

typedef struct {
	SPC_Combine3Fn combine;
	SPC_FreeFn destroy;
} SPC_Combine3;

typedef struct {
	SPC_Combine2Fn combine;
	SPC_FreeFn destroy;
} SPC_Combine2;

typedef struct {
	SPC_MapFn transform;
	SPC_FreeFn destroy;
} SPC_Map;

typedef struct {
	void *(*init)(void);
	void (*append)(void *self, void *item);
	SPC_FreeFn destroy;
} SPC_Container;

struct SPC_Parser {
    ParseFn parse;
    void *ctx;
};

SPC_Parser *spc_map(SPC_Parser *parser, SPC_Map transform);
SPC_Parser *spc_choice(SPC_Parser *left, SPC_Parser *right);
SPC_Parser *spc_sequence(SPC_Parser *left, SPC_Parser *right, SPC_Combine2 combine);
SPC_Parser *spc_keep_right(SPC_Parser *left, SPC_Parser *right);
SPC_Parser *spc_keep_left(SPC_Parser *left, SPC_Parser *right);
SPC_Parser *spc_between(SPC_Parser *left, SPC_Parser *middle, SPC_Parser *right);
SPC_Parser *spc_lazy(SPC_Parser **ref);
SPC_Parser *spc_chain_left(SPC_Parser *operand, SPC_Parser *operator, SPC_Combine3 combine);
SPC_Parser *spc_chain_right(SPC_Parser *operand, SPC_Parser *operator, SPC_Combine3 combine);
SPC_Parser *spc_sep_by(SPC_Parser *item, SPC_Parser *separator, SPC_Container cont);
SPC_Parser *spc_sep_by1(SPC_Parser *item, SPC_Parser *separator, SPC_Container cont);
SPC_Parser *spc_many(SPC_Parser *item, SPC_Container cont);
SPC_Parser *spc_many1(SPC_Parser *item, SPC_Container cont);

static SPC_Result spc_success(void *data, SPC_FreeFn ff) {
	return (SPC_Result){true, NULL, data, NULL, ff};
}

static SPC_Result spc_error(void *error, SPC_FreeFn ff) {
	return (SPC_Result){false, error, NULL, ff, NULL};
}

#endif // SPC_H_

#ifdef SPC_IMPLEMENTATION

typedef struct {
	SPC_Parser *parser;
	SPC_Map transform;
} SPC_MapCtx;

SPC_Result spc_parse_map(SPC_Parser *self, SPC_Input *input) {
	SPC_MapCtx *ctx = self->ctx;

	input->mark(input->self);

	SPC_Result res = ctx->parser->parse(ctx->parser, input);
	if (!res.success) {
		input->rewind(input->self);
		return res;
	}

	input->unmark(input->self);

	return spc_success(
		ctx->transform.transform(res.data),
		ctx->transform.destroy
	);
}

SPC_Parser *spc_map(SPC_Parser *parser, SPC_Map transform) {
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

SPC_Result spc_parse_choice(SPC_Parser *self, SPC_Input *input) {
    SPC_ChoiceCtx *ctx = self->ctx;

	input->mark(input->self);

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (left.success) {
		input->unmark(input->self);
        return left;
    }

	input->rewind(input->self);
	input->mark(input->self);

	if (left.free_error) {
		left.free_error(left.error);
	}

    SPC_Result right = ctx->right->parse(ctx->right, input);
	if (right.success) {
		input->unmark(input->self);
		return right;
	}

	input->rewind(input->self);

	return right;
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
	SPC_Combine2 combine;
} SPC_SequenceCtx;

SPC_Result spc_parse_sequence(SPC_Parser *self, SPC_Input *input) {
    SPC_SequenceCtx *ctx = self->ctx;

	input->mark(input->self);

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (!left.success) {
		input->rewind(input->self);
		return left;
	}

	SPC_Result right = ctx->right->parse(ctx->right, input);
    if (!right.success) {
		input->rewind(input->self);
		return right;
	}

	input->unmark(input->self);

    return spc_success(
		ctx->combine.combine(left.data, right.data),
		ctx->combine.destroy
	);
}

SPC_Parser *spc_sequence(SPC_Parser *left, SPC_Parser *right, SPC_Combine2 combine) {
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

SPC_Result parse_keep_right(SPC_Parser *self, SPC_Input *input) {
    SPC_KeepRightCtx *ctx = self->ctx;

	input->mark(input->self);

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (!left.success) {
		input->rewind(input->self);
		return left;
	}

	SPC_Result right = ctx->right->parse(ctx->right, input);
    if (!right.success) {
		input->rewind(input->self);
		return right;
	}

	input->unmark(input->self);

    return spc_success(right.data, right.free_data);
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

SPC_Result spc_parse_keep_left(SPC_Parser *self, SPC_Input *input) {
    KeepLeftCtx *ctx = self->ctx;

	input->mark(input->self);

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (!left.success) {
		input->rewind(input->self);
		return left;
	}

	SPC_Result right = ctx->right->parse(ctx->right, input);
    if (!right.success) {
		input->rewind(input->self);
		return right;
	}

	input->unmark(input->self);

    return spc_success(left.data, left.free_data);
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

SPC_Result spc_parse_between(SPC_Parser *self, SPC_Input *input) {
    SPC_BetweenCtx *ctx = self->ctx;

	input->mark(input->self);

    SPC_Result left = ctx->left->parse(ctx->left, input);
    if (!left.success) {
		input->rewind(input->self);
		return left;
	}

	SPC_Result middle = ctx->middle->parse(ctx->middle, input);
    if (!middle.success) {
		input->rewind(input->self);
		return middle;
	}

	SPC_Result right = ctx->right->parse(ctx->right, input);
    if (!right.success) {
		input->rewind(input->self);
		return right;
	}

	input->unmark(input->self);

    return spc_success(middle.data, middle.free_data);
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

SPC_Result spc_parse_lazy(SPC_Parser *self, SPC_Input *input) {
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
	SPC_Combine3 combine;
} SPC_ChainLeftCtx;

SPC_Result spc_parse_chain_left(SPC_Parser *self, SPC_Input *input) {
	SPC_ChainLeftCtx *ctx = self->ctx;

	input->mark(input->self);

	SPC_Result left = ctx->operand->parse(ctx->operand, input);
	if (!left.success) {
		input->rewind(input->self);
		return left;
	}

	input->unmark(input->self);

	while (true) {
		input->mark(input->self);

		SPC_Result middle = ctx->operator->parse(ctx->operator, input);
		if (!middle.success) {
			input->rewind(input->self);
			break;
		}

		SPC_Result right = ctx->operand->parse(ctx->operand, input);
		if (!right.success) {
			input->rewind(input->self);
			return right;
		}

		left.data = ctx->combine.combine(left.data, middle.data, right.data);
		left.free_data = ctx->combine.destroy;

		input->unmark(input->self);
	}

	return left;
}

SPC_Parser *spc_chain_left(SPC_Parser *operand, SPC_Parser *operator, SPC_Combine3 combine) {
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
	SPC_Combine3 combine;
} SPC_ChainRightCtx;

SPC_Result spc_parse_chain_right(SPC_Parser *self, SPC_Input *input) {
	SPC_ChainRightCtx *ctx = self->ctx;

	input->mark(input->self);

	SPC_Result left = ctx->operand->parse(ctx->operand, input);
	if (!left.success) {
		input->rewind(input->self);
		return left;
	}

	input->unmark(input->self);

	input->mark(input->self);

	SPC_Result middle = ctx->operator->parse(ctx->operator, input);
	if (!middle.success) {
		input->rewind(input->self);
		return left;
	}

	SPC_Parser req_parser = {spc_parse_chain_right, ctx};
	SPC_Result right = req_parser.parse(&req_parser, input);
	if (!right.success) {
		input->rewind(input->self);
		return right;
	}

	input->unmark(input->self);

	return spc_success(
		ctx->combine.combine(left.data, middle.data, right.data),
		ctx->combine.destroy
	);
}

SPC_Parser *spc_chain_right(SPC_Parser *operand, SPC_Parser *operator, SPC_Combine3 combine) {
	SPC_ChainRightCtx *ctx = malloc(sizeof(*ctx));
	ctx->operand = operand;
	ctx->operator = operator;
	ctx->combine = combine;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_chain_right;
	p->ctx = ctx;
	return p;
}

typedef struct {
	SPC_Parser *item;
	SPC_Parser *separator;
	SPC_Container container;
	bool at_least_one;
} SPC_SepByCtx;

SPC_Result spc_parse_sep_by(SPC_Parser *self, SPC_Input *input) {
	SPC_SepByCtx *ctx = self->ctx;

	input->mark(input->self);

	SPC_Result first = ctx->item->parse(ctx->item, input);
	if (!first.success) {
		input->rewind(input->self);

		if (ctx->at_least_one) {
			return first;
		} else {
			return spc_success(
				ctx->container.init(),
				ctx->container.destroy
			);
		}
	} else {
		input->unmark(input->self);
	}

	void *cont = ctx->container.init();
	ctx->container.append(cont, first.data);

	while (true) {
		input->mark(input->self);

		SPC_Result sep = ctx->separator->parse(ctx->separator, input);
		if (!sep.success) break;

		SPC_Result next = ctx->item->parse(ctx->item, input);
		if (!next.success) break;

		ctx->container.append(cont, next.data);

		input->unmark(input->self);
	}

	input->rewind(input->self);

	return spc_success(cont,(assert(1), NULL));
}

SPC_Parser *spc_sep_by(SPC_Parser *item, SPC_Parser *separator, SPC_Container cont) {
	SPC_SepByCtx *ctx = malloc(sizeof(*ctx));
	ctx->item = item;
	ctx->separator = separator;
	ctx->container = cont;
	ctx->at_least_one = false;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_sep_by;
	p->ctx = ctx;
	return p;
}

SPC_Parser *spc_sep_by1(SPC_Parser *item, SPC_Parser *separator, SPC_Container cont) {
	SPC_SepByCtx *ctx = malloc(sizeof(*ctx));
	ctx->item = item;
	ctx->separator = separator;
	ctx->container = cont;
	ctx->at_least_one = false;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_sep_by;
	p->ctx = ctx;
	return p;
}

typedef struct {
	SPC_Parser *item;
	SPC_Container container;
	bool at_least_one;
} SPC_ManyCtx;

SPC_Result spc_parse_many(SPC_Parser *self, SPC_Input *input) {
	SPC_ManyCtx *ctx = self->ctx;

	input->mark(input->self);

	SPC_Result first = ctx->item->parse(ctx->item, input);
	if (!first.success) {
		input->rewind(input->self);

		if (ctx->at_least_one) {
			return first;
		} else {
			return spc_success(ctx->container.init(), (assert(1), NULL));
		}
	} else {
		input->unmark(input->self);
	}

	void *cont = ctx->container.init();
	ctx->container.append(cont, first.data);

	while (true) {
		input->mark(input->self);

		SPC_Result item = ctx->item->parse(ctx->item, input);
		if (!item.success) break;

		ctx->container.append(cont, item.data);

		input->unmark(input->self);
	}

	input->rewind(input->self);

	return spc_success(cont, (assert(1), NULL));
}

SPC_Parser *spc_many(SPC_Parser *item, SPC_Container cont) {
	SPC_ManyCtx *ctx = malloc(sizeof(*ctx));
	ctx->item = item;
	ctx->container = cont;
	ctx->at_least_one = false;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_many;
	p->ctx = ctx;
	return p;
}

SPC_Parser *spc_many1(SPC_Parser *item, SPC_Container cont) {
	SPC_ManyCtx *ctx = malloc(sizeof(*ctx));
	ctx->item = item;
	ctx->container = cont;
	ctx->at_least_one = true;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_many;
	p->ctx = ctx;
	return p;
}

#endif // SPC_IMPLEMENTATION
