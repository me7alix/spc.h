#ifndef SPC_H_
#define SPC_H_

#include <stdlib.h>
#include <stdarg.h>

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
	void (*mark)(void*);
	void (*unmark)(void*);
	void (*rewind)(void*);
} SPC_Input;

typedef struct SPC_Parser SPC_Parser;
typedef SPC_Result (*SPC_ParseFn)(SPC_Parser*, SPC_Input*);
typedef void *(*SPC_FoldFn)(int n, void**);
typedef void *(*SPC_MapFn)(void*);

typedef struct {
	SPC_FoldFn fold;
	SPC_FreeFn destroy;
} SPC_Fold;

typedef struct {
	SPC_MapFn transform;
	SPC_FreeFn destroy;
} SPC_Map;

struct SPC_Parser {
    SPC_ParseFn parse;
    void *ctx;
};

SPC_Parser *spc_map(SPC_Parser *parser, SPC_Map transform);
SPC_Parser *spc_choice(int n, ...);
SPC_Parser *spc_sequence(int n, SPC_Fold fold, ...);
SPC_Parser *spc_keep_right(SPC_Parser *left, SPC_Parser *right);
SPC_Parser *spc_keep_left(SPC_Parser *left, SPC_Parser *right);
SPC_Parser *spc_between(SPC_Parser *left, SPC_Parser *middle, SPC_Parser *right);
SPC_Parser *spc_lazy(SPC_Parser **ref);
SPC_Parser *spc_chain_left(SPC_Parser *operand, SPC_Parser *operator, SPC_Fold fold);
SPC_Parser *spc_chain_right(SPC_Parser *operand, SPC_Parser *operator, SPC_Fold fold);
SPC_Parser *spc_sep_by(SPC_Parser *item, SPC_Parser *separator, SPC_Fold fold);
SPC_Parser *spc_sep_by1(SPC_Parser *item, SPC_Parser *separator, SPC_Fold fold);
SPC_Parser *spc_many(SPC_Parser *item, SPC_Fold fold);
SPC_Parser *spc_many1(SPC_Parser *item, SPC_Fold fold);

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
	SPC_Parser **items;
	int count;
} SPC_ChoiceCtx;

SPC_Result spc_parse_choice(SPC_Parser *self, SPC_Input *input) {
    SPC_ChoiceCtx *ctx = self->ctx;
	SPC_Result res;

	for (int i = 0; i < ctx->count; i++) {
		input->mark(input->self);

		res = ctx->items[i]->parse(ctx->items[i], input);
		if (res.success) {
			input->unmark(input->self);
			return res;
		}

		input->rewind(input->self);

		if (res.free_error) {
			res.free_error(res.error);
		}
	}

	return res;
}

SPC_Parser *spc_choice(int n, ...) {
    SPC_ChoiceCtx *ctx = malloc(sizeof(*ctx));
	ctx->items = malloc(sizeof(void*) * n);
	ctx->count = n;

	va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        ctx->items[i] = va_arg(ap, SPC_Parser*);
    va_end(ap);

    SPC_Parser *p = malloc(sizeof(*p));
    p->parse = spc_parse_choice;
    p->ctx = ctx;
    return p;
}

typedef struct {
    SPC_Parser **items;
	int count;
	SPC_Fold fold;
} SPC_SequenceCtx;

SPC_Result spc_parse_sequence(SPC_Parser *self, SPC_Input *input) {
    SPC_SequenceCtx *ctx = self->ctx;
	void *items[ctx->count];
	SPC_FreeFn ffs[ctx->count];

	input->mark(input->self);

	for (int i = 0; i < ctx->count; i++) {
		SPC_Result res = ctx->items[i]->parse(ctx->items[i], input);
		if (!res.success) {
			for (int j = 0; j < i; j++) {
				ffs[j](items[j]);
			}

			input->rewind(input->self);
			return res;
		}

		items[i] = res.data;
		ffs[i] = res.free_data;
	}

	input->unmark(input->self);

    return spc_success(
		ctx->fold.fold(ctx->count, items),
		ctx->fold.destroy
	);
}

SPC_Parser *spc_sequence(int n, SPC_Fold fold, ...) {
    SPC_SequenceCtx *ctx = malloc(sizeof(*ctx));
	ctx->fold = fold;
	ctx->items = malloc(sizeof(void*) * n);
	ctx->count = n;

	va_list ap;
    va_start(ap, fold);
    for (int i = 0; i < n; i++)
        ctx->items[i] = va_arg(ap, SPC_Parser*);
    va_end(ap);

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
	SPC_Fold fold;
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

		void *arr[] = {left.data, middle.data, right.data};
		left.data = ctx->fold.fold(3, arr);
		left.free_data = ctx->fold.destroy;

		input->unmark(input->self);
	}

	return left;
}

SPC_Parser *spc_chain_left(SPC_Parser *operand, SPC_Parser *operator, SPC_Fold fold) {
	SPC_ChainLeftCtx *ctx = malloc(sizeof(*ctx));
	ctx->operand = operand;
	ctx->operator = operator;
	ctx->fold = fold;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_chain_left;
	p->ctx = ctx;
	return p;
}

typedef struct {
	SPC_Parser *operand;
	SPC_Parser *operator;
	SPC_Fold fold;
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

	void *arr[] = {left.data, middle.data, right.data};
	return spc_success(
		ctx->fold.fold(3, arr),
		ctx->fold.destroy
	);
}

SPC_Parser *spc_chain_right(SPC_Parser *operand, SPC_Parser *operator, SPC_Fold fold) {
	SPC_ChainRightCtx *ctx = malloc(sizeof(*ctx));
	ctx->operand = operand;
	ctx->operator = operator;
	ctx->fold = fold;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_chain_right;
	p->ctx = ctx;
	return p;
}

typedef struct {
	void **items;
	size_t count;
	size_t capacity;
} SPC_DynArr;

void spc_dynarr_append(SPC_DynArr *da, void *item) {
	if (da->capacity == 0) da->capacity = 128;
	if (da->count >= da->capacity - 1 || !da->items)
		da->items = realloc(da->items, sizeof(void*) * (da->capacity *= 2));
	da->items[da->count++] = item;
}

typedef struct {
	SPC_Parser *item;
	SPC_Parser *separator;
	SPC_Fold fold;
	bool at_least_one;
} SPC_SepByCtx;

void spc_dynarr_free(SPC_DynArr *da) {
	if (da->items) {
		free(da->items);
		da->items = NULL;
	}
}

SPC_Result spc_parse_sep_by(SPC_Parser *self, SPC_Input *input) {
	SPC_SepByCtx *ctx = self->ctx;

	input->mark(input->self);

	SPC_Result first = ctx->item->parse(ctx->item, input);
	if (!first.success) {
		input->rewind(input->self);

		if (ctx->at_least_one) {
			return first;
		}

		return spc_success(
			ctx->fold.fold(0, NULL),
			ctx->fold.destroy
		);
	}

	input->unmark(input->self);

	SPC_DynArr arr = {0};
	spc_dynarr_append(&arr, first.data);

	while (true) {
		input->mark(input->self);

		SPC_Result sep = ctx->separator->parse(ctx->separator, input);
		if (!sep.success) break;

		SPC_Result next = ctx->item->parse(ctx->item, input);
		if (!next.success) break;

		spc_dynarr_append(&arr, next.data);

		input->unmark(input->self);
	}

	input->rewind(input->self);

	SPC_Result res = spc_success(
		ctx->fold.fold(arr.count, arr.items),
		ctx->fold.destroy
	);

	spc_dynarr_free(&arr);

	return res;
}

SPC_Parser *spc_sep_by(SPC_Parser *item, SPC_Parser *separator, SPC_Fold fold) {
	SPC_SepByCtx *ctx = malloc(sizeof(*ctx));
	ctx->item = item;
	ctx->separator = separator;
	ctx->at_least_one = false;
	ctx->fold = fold;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_sep_by;
	p->ctx = ctx;
	return p;
}

SPC_Parser *spc_sep_by1(SPC_Parser *item, SPC_Parser *separator, SPC_Fold fold) {
	SPC_SepByCtx *ctx = malloc(sizeof(*ctx));
	ctx->item = item;
	ctx->separator = separator;
	ctx->at_least_one = false;
	ctx->fold = fold;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_sep_by;
	p->ctx = ctx;
	return p;
}

typedef struct {
	SPC_Parser *item;
	SPC_Fold fold;
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
		}

		return spc_success(
			ctx->fold.fold(0, NULL),
			ctx->fold.destroy
		);
	}

	input->unmark(input->self);

	SPC_DynArr arr = {0};
	spc_dynarr_append(&arr, first.data);

	while (true) {
		input->mark(input->self);

		SPC_Result item = ctx->item->parse(ctx->item, input);
		if (!item.success) break;

		spc_dynarr_append(&arr, item.data);

		input->unmark(input->self);
	}

	input->rewind(input->self);

	SPC_Result res = spc_success(
		ctx->fold.fold(arr.count, arr.items),
		ctx->fold.destroy
	);

	spc_dynarr_free(&arr);

	return res;
}

SPC_Parser *spc_many(SPC_Parser *item, SPC_Fold fold) {
	SPC_ManyCtx *ctx = malloc(sizeof(*ctx));
	ctx->item = item;
	ctx->fold = fold;
	ctx->at_least_one = false;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_many;
	p->ctx = ctx;
	return p;
}

SPC_Parser *spc_many1(SPC_Parser *item, SPC_Fold fold) {
	SPC_ManyCtx *ctx = malloc(sizeof(*ctx));
	ctx->item = item;
	ctx->fold = fold;
	ctx->at_least_one = true;

	SPC_Parser *p = malloc(sizeof(*p));
	p->parse = spc_parse_many;
	p->ctx = ctx;
	return p;
}

#endif // SPC_IMPLEMENTATION
