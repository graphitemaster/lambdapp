/*
* Copyright (C) 2014
*   Wolfgang Bumiller
*   Dale Weiler
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of
* this software and associated documentation files (the "Software"), to deal in
* the Software without restriction, including without limitation the rights to
* use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
* of the Software, and to permit persons to whom the Software is furnished to do
* so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>

typedef struct {
    size_t begin;
    size_t length;
} lambda_range_t;

typedef struct {
    const char *file;
    char       *data;
    size_t      length;
    size_t      line;
} lambda_source_t;

typedef struct {
    size_t         start;
    lambda_range_t type;
    lambda_range_t args;
    lambda_range_t body;
} lambda_t;

typedef struct {
    union {
        char     *chars;
        lambda_t *funcs;
    };
    size_t size;
    size_t elements;
    size_t length;
} lambda_vector_t;

/* Vector */
static void lambda_vector_init(lambda_vector_t *vec, bool funcs) {
    vec->length   = 32;
    vec->size     = funcs ? sizeof(lambda_t) : 1;
    vec->chars    = (char *)malloc(vec->length * vec->size);
    vec->elements = 0;
}

static void lambda_vector_destroy(lambda_vector_t *vec) {
    free(vec->chars);
}

static void lambda_vector_resize(lambda_vector_t *vec) {
    if (vec->elements == vec->length) {
        vec->length <<= 1;
        vec->chars    = (char *)realloc(vec->chars, vec->length * vec->size);
    }
}

static void lambda_vector_push_char(lambda_vector_t *vec, char ch) {
    lambda_vector_resize(vec);
    vec->chars[vec->elements++] = ch;
}

static void lambda_vector_push_lambda(lambda_vector_t *vec, lambda_t lambda) {
    lambda_vector_resize(vec);
    vec->funcs[vec->elements++] = lambda;
}

/* Source */
static bool parse_open(lambda_source_t *source, const char *file) {
    FILE *handle;
    if (!(handle = fopen(file, "r")))
        return false;

    source->file = file;
    source->line = 1;

    fseek(handle, 0, SEEK_END);
    source->length = ftell(handle);
    fseek(handle, 0, SEEK_SET);

    if (!(source->data = (char *)malloc(source->length + 1)))
        goto parse_open_error_data;
    if (fread(source->data, source->length, 1, handle) != 1)
        goto parse_open_error_file;

    source->data[source->length] = '\0';
    fclose(handle);
    return true;

parse_open_error_file:
    free(source->data);
parse_open_error_data:
    fclose(handle);
    return false;
}

static void parse_close(lambda_source_t *source) {
    free(source->data);
}

/* Parser */
static size_t parse_skip_string(lambda_source_t *source, size_t i, char check) {
    while (i != source->length) {
        if (source->data[i] == check) {
            ++i;
            break;
        } else if (source->data[i] == '\\') {
            if (++i == source->length)
                break;
        }
        i++;
    }
    return i;
}

static size_t parse_skip_white(lambda_source_t *source, size_t i) {
    while (i != source->length && isspace(source->data[i])) {
        if (source->data[i] == '\n')
            source->line++;
        ++i;
    }
    return i;
}

static size_t parse_skip_to(lambda_source_t *source, size_t i, char check) {
    while (i != source->length && source->data[i] != check) {
        if (source->data[i] == '\n')
            source->line++;
        ++i;
    }
    return i;
}

static void parse_error(lambda_source_t *source, const char *message, ...) {
    char buffer[2048];
    va_list va;
    va_start(va, message);
    vsnprintf(buffer, sizeof(buffer), message, va);
    va_end(va);
    fprintf(stderr, "%s:%zu error: %s\n", source->file, source->line, buffer);
}

static size_t parse(lambda_source_t *source, lambda_vector_t *lambdas, size_t j, bool inlambda, bool special);

static size_t parse_word(lambda_source_t *source, lambda_vector_t *lambdas, size_t j, size_t i) {
    if (j != i) {
        if (strncmp(source->data + j, "lambda", i - j) == 0)
            return parse(source, lambdas, i, true, false);
    }
    if (source->data[i] == '\n')
        source->line++;
    else if (!strncmp(source->data + i, "//", 2)) {
        /* Single line comments */
        while (i != source->length) {
            if (source->data[i] == '\n') {
                ++i;
                break;
            }
            ++i;
        }
    } else if (!strncmp(source->data + i, "/*", 2)) {
        /* Multi line comments */
        while (i != source->length) {
            if (!strncmp(source->data + i, "*/", 2)) {
                i += 2;
                break;
            }
            ++i;
        }
    }
    return i;
}

static size_t parse(lambda_source_t *source, lambda_vector_t *lambdas, size_t i, bool inlambda, bool special) {
    lambda_vector_t parens;
    lambda_t        lambda = { 0 };

    lambda_vector_init(&parens, false);

    if (inlambda) {
        lambda.start = i - 6;
        i = parse_skip_white(source, i);
        lambda.type.begin = i;
        if (source->data[i] == '(') {
            if ((i = parse(source, lambdas, i, false, true)) == -1) {
                lambda_vector_destroy(&parens);
                return -1;
            }
        }
        i = parse_skip_to(source, i, '(');
        lambda.type.length = i - lambda.type.begin;
        lambda.args.begin = i;
        if ((i = parse(source, lambdas, i, false, true)) == -1) {
            lambda_vector_destroy(&parens);
            return -1;
        }
        lambda.args.length = i - lambda.args.begin + 1;
        i = parse_skip_to(source, i, '{');
        lambda.body.begin = i;
    }

    size_t j = i;
    while (i < source->length) {
        if (source->data[i] == '"') {
            if (!special && (i = parse_word(source, lambdas, j, i)) == -1)
                goto parse_error;
            j = i = parse_skip_string(source, i+1, source->data[i]);
        } else if (source->data[i] == '\'') {
            if (!special && (i = parse_word(source, lambdas, j, i)) == -1)
                goto parse_error;
            j = i = parse_skip_string(source, i+1, source->data[i]);
        } else if (strchr("([{", source->data[i])) {
            if (!special && (i = parse_word(source, lambdas, j, i)) == -1)
                goto parse_error;
            lambda_vector_push_char(&parens, strchr("([{)]}", source->data[i])[3]);
            j = ++i;
        } else if (strchr(")]}", source->data[i])) {
            if (!parens.elements) {
                parse_error(source, "too many closing parenthesis");
                goto parse_error;
            }
            char back = parens.chars[parens.elements >= 1 ? parens.elements - 1 : 0];
            if (source->data[i] != back) {
                parse_error(source, "mismatching `%c' and `%c'", back, source->data[i]);
                goto parse_error;
            }
            //if (source->data[i + 1] == back) {
            //    parse_error(source, "unexpected token `%c'", back);
            //    goto parse_error;
            //}
            if (parens.elements != 0)
                parens.elements--;
            if (special && source->data[i] == ')' && !parens.elements) {
                lambda_vector_destroy(&parens);
                return i;
            }
            if (inlambda) {
                if (source->data[i] == '}' && !parens.elements) {
                    lambda.body.length = i - lambda.body.begin;
                    lambda_vector_push_lambda(lambdas, lambda);
                    lambda_vector_destroy(&parens);
                    return i;
                }
            }
            if (!special && (i = parse_word(source, lambdas, j, i)) == -1)
                goto parse_error;
            j = ++i;
        } else if (source->data[i] != '_' && !isalnum(source->data[i])) {
            if (!special && (i = parse_word(source, lambdas, j, i)) == -1)
                goto parse_error;
            j = ++i;
        } else
            ++i;
    }

    lambda_vector_destroy(&parens);
    return i;

parse_error:
    lambda_vector_destroy(&parens);
    return -1;
}

/* Generator */
int generate_compare(const void *lhs, const void *rhs) {
    const lambda_t *a = (const lambda_t *)lhs;
    const lambda_t *b = (const lambda_t *)rhs;

    return a->start > b->start;
}

static void generate_sliced(const char *source, size_t i, size_t j, lambda_vector_t *lambdas, size_t k) {
    while (j) {
        if (k == lambdas->elements || lambdas->funcs[k].start > i + j) {
            printf("%.*s", (int)(j), source + i);
            return;
        }

        printf("%.*s ({ %.*s lambda_%zu%.*s; &lambda_%zu; })",
            (int)(lambdas->funcs[k].start - i),
            source + i,
            (int)(lambdas->funcs[k].type.length),
            source + lambdas->funcs[k].type.begin,
            k,
            (int)(lambdas->funcs[k].args.length),
            source + lambdas->funcs[k].args.begin,
            k
        );

        j -= lambdas->funcs[k].body.begin + lambdas->funcs[k].body.length + 1 - i;
        i += lambdas->funcs[k].body.begin + lambdas->funcs[k].body.length + 1 - i;

        for (++k; k != lambdas->elements && lambdas->funcs[k].start < i; k++)
            ;
    }
}

static void generate_begin(lambda_source_t *source, lambda_t *lambda, size_t name) {
    printf("%.*s lambda_%zu%.*s",
        (int)(lambda->type.length), source->data + lambda->type.begin,
        name,
        (int)(lambda->args.length), source->data + lambda->args.begin
    );
}

static void generate(lambda_source_t *source) {
    lambda_vector_t lambdas;
    lambda_vector_init(&lambdas, true);
    if (parse(source, &lambdas, 0, false, false) == -1) {
        lambda_vector_destroy(&lambdas);
        return;
    }

    qsort(lambdas.funcs, lambdas.elements, sizeof(lambda_t), &generate_compare);

    /* Enable this to print prototypes first */
#if 0
    for (size_t i = 0; i < lambdas.elements; i++) {
        lambda_t *lambda = &lambdas.funcs[i];
        generate_begin(source, lambda, i);
        printf(";\n");
    }
#endif

    generate_sliced(source->data, 0, source->length, &lambdas, 0);

    for (size_t i = 0; i < lambdas.elements; i++) {
        lambda_t *lambda = &lambdas.funcs[i];
        generate_begin(source, lambda, i);
        printf(" ");
        generate_sliced(source->data, lambda->body.begin, lambda->body.length + 1, &lambdas, i + 1);
        printf("\n");
    }

    lambda_vector_destroy(&lambdas);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file>\n", *argv);
        return 1;
    }

    lambda_source_t source;
    if (!parse_open(&source, argv[1])) {
        fprintf(stderr, "failed to open file %s %s\n", *argv, strerror(errno));
        return 1;
    }

    generate(&source);
    parse_close(&source);

    return 0;
}
