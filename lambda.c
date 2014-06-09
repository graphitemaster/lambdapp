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
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>

#define isalpha(a) ((((unsigned)(a)|32)-'a') < 26)
#define isdigit(a) (((unsigned)(a)-'0') < 10)
#define isalnum(a) (isalpha(a) || isdigit(a))
#define isspace(a) (((a) >= '\t' && (a) <= '\r') || (a) == ' ')

typedef struct {
    size_t begin;
    size_t length;
} lambda_range_t;

typedef struct {
    const char *file;
    char        uuid[128];
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
        size_t   *positions;
    };
    size_t size;
    size_t elements;
    size_t length;
} lambda_vector_t;

typedef struct {
  lambda_vector_t lambdas;
  lambda_vector_t positions;
} parse_data_t;

static size_t parse(lambda_source_t *source, parse_data_t *data, size_t j, bool inlambda, bool special);

/* Vector */
static inline void lambda_vector_init(lambda_vector_t *vec, size_t size) {
    vec->length   = 32;
    vec->size     = size;
    vec->chars    = (char *)malloc(vec->length * vec->size);
    vec->elements = 0;
}

static inline void lambda_vector_destroy(lambda_vector_t *vec) {
    free(vec->chars);
}

static inline void lambda_vector_resize(lambda_vector_t *vec) {
    if (vec->elements != vec->length)
        return;
    vec->length <<= 1;
    vec->chars    = (char *)realloc(vec->chars, vec->length * vec->size);
}

static inline void lambda_vector_push_char(lambda_vector_t *vec, char ch) {
    lambda_vector_resize(vec);
    vec->chars[vec->elements++] = ch;
}

static inline void lambda_vector_push_lambda(lambda_vector_t *vec, lambda_t lambda) {
    lambda_vector_resize(vec);
    vec->funcs[vec->elements++] = lambda;
}

static inline void lambda_vector_push_position(lambda_vector_t *vec, size_t pos) {
    lambda_vector_resize(vec);
    vec->positions[vec->elements++] = pos;
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

static inline void parse_close(lambda_source_t *source) {
    free(source->data);
}

/* Parser */
static inline size_t parse_skip_string(lambda_source_t *source, size_t i, char check) {
    while (i != source->length) {
        if (source->data[i] == check)
            return i + 1;
        else if (source->data[i] == '\\')
            if (++i == source->length)
                break;
        ++i;
    }
    return i;
}

static inline size_t parse_skip_white(lambda_source_t *source, size_t i) {
    while (i != source->length && isspace(source->data[i])) {
        if (source->data[i] == '\n')
            source->line++;
        ++i;
    }
    return i;
}

static inline size_t parse_skip_to(lambda_source_t *source, size_t i, char check) {
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

static size_t parse_word(lambda_source_t *source, parse_data_t *data, size_t j, size_t i) {
    if (j != i) {
        if (strncmp(source->data + j, "lambda", i - j) == 0)
            return parse(source, data, i, true, false);
    }
    if (source->data[i] == '\n')
        source->line++;
    else if (!strncmp(source->data + i, "//", 2)) {
        /* Single line comments */
        i = strchr(source->data + i, '\n') - source->data;
    } else if (!strncmp(source->data + i, "/*", 2)) {
        /* Multi line comments */
        i = strstr(source->data + i, "*/") - source->data;
    }
    return i;
}

#define ERROR ((size_t)-1)

static size_t parse(lambda_source_t *source, parse_data_t *data, size_t i, bool inlambda, bool special) {
    lambda_vector_t parens;
    lambda_t        lambda;
    bool            mark = (!inlambda && !special);
    size_t          protopos = i;
    bool            protomove = true;
    bool            preprocessor = false;
    /* 'mark' actually means this is the outer most call and we should
     * remember where to put prototypes now!
     * when protomove is true we move the protopos along whitespace so that
     * the lambdas don't get stuck to the tail of hte previous functions.
     * Also we need to put lambdas after #include lines so if we encounter
     * a preprocessor directive we create another position marker starting
     * at the nest new line
     */

    memset(&lambda, 0, sizeof(lambda_t));
    lambda_vector_init(&parens, sizeof(char));


    if (inlambda) {
        lambda.start = i - 6;
        i = parse_skip_white(source, i);
        lambda.type.begin = i;
        if (source->data[i] == '(') {
            if ((i = parse(source, data, i, false, true)) == ERROR) {
                lambda_vector_destroy(&parens);
                return ERROR;
            }
        }
        i = parse_skip_to(source, i, '(');
        lambda.type.length = i - lambda.type.begin;
        lambda.args.begin = i;
        if ((i = parse(source, data, i, false, true)) == ERROR) {
            lambda_vector_destroy(&parens);
            return ERROR;
        }
        lambda.args.length = i - lambda.args.begin + 1;
        i = parse_skip_to(source, i, '{');
        lambda.body.begin = i;
    }

    size_t j = i;
    while (i < source->length) {
        if (mark && !parens.elements) {
            if (protomove) {
                if (isspace(source->data[i])) {
                    protopos = j = ++i;
                    continue;
                }
                protomove = false;
                lambda_vector_push_position(&data->positions, protopos);
            }

            if (source->data[i] == ';') {
                if (!special && (i = parse_word(source, data, j, i)) == ERROR)
                    goto parse_error;
                j = ++i;
                protomove = true;
                protopos  = i;
                continue;
            }

            if (source->data[i] == '#') {
                if (!special && (i = parse_word(source, data, j, i)) == ERROR)
                    goto parse_error;
                j = ++i;
                protomove = false;
                protopos  = i;
                preprocessor = true;
                continue;
            }
            if (preprocessor && source->data[i] == '\n') {
                if (!special && (i = parse_word(source, data, j, i)) == ERROR)
                    goto parse_error;
                j = ++i;
                protomove = true;
                protopos  = i;
                preprocessor = false;
                continue;
            }
        }

        if (source->data[i] == '"') {
            if (!special && (i = parse_word(source, data, j, i)) == ERROR)
                goto parse_error;
            j = i = parse_skip_string(source, i+1, source->data[i]);
        } else if (source->data[i] == '\'') {
            if (!special && (i = parse_word(source, data, j, i)) == ERROR)
                goto parse_error;
            j = i = parse_skip_string(source, i+1, source->data[i]);
        } else if (strchr("([{", source->data[i])) {
            if (!special && (i = parse_word(source, data, j, i)) == ERROR)
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
            if (parens.elements != 0)
                parens.elements--;
            if (special && source->data[i] == ')' && !parens.elements) {
                lambda_vector_destroy(&parens);
                return i;
            }
            if (inlambda) {
                if (source->data[i] == '}' && !parens.elements) {
                    lambda.body.length = i - lambda.body.begin;
                    lambda_vector_push_lambda(&data->lambdas, lambda);
                    lambda_vector_destroy(&parens);
                    return i;
                }
            }
            bool domark = (mark && !parens.elements && source->data[i] == '}');
            if (!special && (i = parse_word(source, data, j, i)) == ERROR)
                goto parse_error;
            j = ++i;
            if (domark) {
                protopos = i;
                protomove = true;
            }
        } else if (source->data[i] != '_' && !isalnum(source->data[i])) {
            if (!special && (i = parse_word(source, data, j, i)) == ERROR)
                goto parse_error;
            j = ++i;
        } else
            ++i;
    }

    lambda_vector_destroy(&parens);
    return i;

parse_error:
    lambda_vector_destroy(&parens);
    return ERROR;
}

/* Generator */
static inline int generate_compare(const void *lhs, const void *rhs) {
    const lambda_t *a = (const lambda_t *)lhs;
    const lambda_t *b = (const lambda_t *)rhs;

    return a->start - b->start;
}

static inline int compare_size(const void *lhs, const void *rhs) {
    return *((const size_t*)lhs) - *((const size_t*)rhs);
}

static inline void generate_marker(FILE *out, const char *file, size_t line) {
    fprintf(out, "# %zu \"%s\"\n", line, file);
}

static void generate_sliced(FILE *out, const char *source, size_t pos, size_t len, parse_data_t *data, size_t lam, const char *uuid) {
    while (len) {
        if (lam == data->lambdas.elements || data->lambdas.funcs[lam].start > pos + len) {
            fwrite(source + pos, len, 1, out);
            return;
        }

        lambda_t *lambda = &data->lambdas.funcs[lam];
        size_t    length = lambda->body.begin + lambda->body.length + 1 - pos;

        fwrite(source + pos, lambda->start - pos, 1, out);
        fprintf(out, " ({");
        fwrite(source + lambda->type.begin, lambda->type.length, 1, out);
        fprintf(out, " lambda_%s_%zu", uuid, lam);
        fwrite(source + lambda->args.begin, lambda->args.length, 1, out);
        fprintf(out, "; &lambda_%s_%zu; })", uuid, lam);

        len -= length;
        pos += length;

        for (++lam; lam != data->lambdas.elements && data->lambdas.funcs[lam].start < pos; ++lam)
            ;
    }
}

static inline void generate_begin(FILE *out, lambda_source_t *source, lambda_t *lambda, size_t name) {
    fwrite(source->data + lambda->type.begin, lambda->type.length, 1, out);
    fprintf(out, " lambda_%s_%zu", source->uuid, name);
    fwrite(source->data + lambda->args.begin, lambda->args.length, 1, out);
}

static inline void generate_uuid(lambda_source_t *source) {
    /* Unique identifiers via file name */
    char *find = strchr(source->file, '.');
    memcpy(source->uuid, source->file, find - source->file);
    source->uuid[find - source->file] = '\0';
}

static void generate(FILE *out, lambda_source_t *source) {
    parse_data_t data;
    lambda_vector_init(&data.lambdas,   sizeof(data.lambdas.funcs[0]));
    lambda_vector_init(&data.positions, sizeof(data.positions.positions[0]));
    if (parse(source, &data, 0, false, false) == ERROR) {
        lambda_vector_destroy(&data.lambdas);
        lambda_vector_destroy(&data.positions);
        return;
    }

    qsort(data.lambdas.funcs, data.lambdas.elements, sizeof(lambda_t), &generate_compare);
    qsort(data.positions.positions, data.positions.elements, sizeof(size_t), &compare_size);

    generate_uuid(source);
    generate_marker(out, source->file, 1);

    generate_sliced(out, source->data, 0, source->length, &data, 0, source->uuid);

    for (size_t i = 0; i < data.lambdas.elements; i++) {
        lambda_t *lambda = &data.lambdas.funcs[i];
        generate_begin(out, source, lambda, i);
        generate_sliced(out, source->data, lambda->body.begin, lambda->body.length + 1, &data, i + 1, source->uuid);
    }

    /* there are cases where we get no newline at the end of the file */
    fprintf(out, "\n");

    lambda_vector_destroy(&data.lambdas);
    lambda_vector_destroy(&data.positions);
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

    generate(stdout, &source);
    parse_close(&source);

    return 0;
}
