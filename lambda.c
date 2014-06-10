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
    size_t pos;
    size_t line;
} lambda_position_t;

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
    size_t         type_line;
    size_t         body_line;
    size_t         end_line;
} lambda_t;

typedef struct {
    union {
        char              *chars;
        lambda_t          *funcs;
        lambda_position_t *positions;
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

static inline void lambda_vector_push_position(lambda_vector_t *vec, size_t pos, size_t line) {
    lambda_vector_resize(vec);
    vec->positions[vec->elements].pos  = pos;
    vec->positions[vec->elements].line = line;
    vec->elements++;
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
        if (strncmp(source->data + j, "lambda", 6) == 0)
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
        lambda.type_line = source->line;
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
        lambda.body_line  = source->line;
    }

    size_t j = i;
    while (i < source->length) {
        if (mark && !parens.elements) {
            if (protomove) {
                if (isspace(source->data[i])) {
                    if (source->data[i] == '\n')
                        source->line++;
                    protopos = j = ++i;
                    continue;
                }
                protomove = false;
                lambda_vector_push_position(&data->positions, protopos, source->line);
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
                    lambda.end_line = source->line;
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

static inline int compare_position(const void *lhs, const void *rhs) {
    const lambda_position_t *a = (const lambda_position_t *)lhs;
    const lambda_position_t *b = (const lambda_position_t *)rhs;

    return a->pos - b->pos;
}

static inline void generate_marker(FILE *out, const char *file, size_t line) {
    fprintf(out, "# %zu \"%s\"\n", line, file);
}

static void generate_sliced(FILE *out, const char *source, size_t pos, size_t len, parse_data_t *data, size_t lam) {
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
        fprintf(out, " lambda_%zu", lam);
        fwrite(source + lambda->args.begin, lambda->args.length, 1, out);
        fprintf(out, "; &lambda_%zu; })", lam);

        len -= length;
        pos += length;

        for (++lam; lam != data->lambdas.elements && data->lambdas.funcs[lam].start < pos; ++lam)
            ;
    }
}

static inline void generate_begin(FILE *out, lambda_source_t *source, lambda_vector_t *lambdas, size_t idx) {
    fprintf(out, "\n#line %zu\nstatic ", lambdas->funcs[idx].type_line);
    fwrite(source->data + lambdas->funcs[idx].type.begin, lambdas->funcs[idx].type.length, 1, out);
    fprintf(out, " lambda_%zu", idx);
    fwrite(source->data + lambdas->funcs[idx].args.begin, lambdas->funcs[idx].args.length, 1, out);
}

static size_t next_prototype_position(parse_data_t *data, size_t lam, size_t proto) {
    if (lam == data->lambdas.elements)
        return data->positions.elements;
    for (; proto != data->positions.elements; ++proto) {
        if (data->positions.positions[proto].pos > data->lambdas.funcs[lam].start)
            return proto-1;
    }
    return data->positions.elements-1;
}

static void generate_prototypes(FILE *out, lambda_source_t *source, parse_data_t *data, size_t lam, size_t proto) {
    size_t end = (proto+1) == data->positions.elements ? (size_t)-1 : data->positions.positions[proto+1].pos;
    for (; lam != data->lambdas.elements; ++lam) {
        if (data->lambdas.funcs[lam].start > end)
            break;
        generate_begin(out, source, &data->lambdas, lam);
        fprintf(out, ";");
    }
    fprintf(out, "\n");
}

/* when generating the actual code we also take prototype-positioning into account */
static void generate_code(FILE *out, lambda_source_t *source, size_t pos, size_t len, parse_data_t *data, size_t lam) {
    /* we know that positions always has at least 1 element, the 0, so the first search is there */
    size_t proto = next_prototype_position(data, lam, 1);
    while (len) {
        if (lam == data->lambdas.elements || data->lambdas.funcs[lam].start > pos + len) {
            fwrite(source->data + pos, len, 1, out);
            return;
        }

        if (proto != data->positions.elements) {
            lambda_position_t *lambdapos = &data->positions.positions[proto];
            size_t point = lambdapos->pos;
            if (pos < point && pos+len >= point) {
                /* we insert prototypes here! */
                size_t length = point - pos;
                fwrite(source->data + pos, length, 1, out);
                generate_prototypes(out, source, data, lam, proto);
                fprintf(out, "\n#line %zu\n", lambdapos->line);
                len -= length;
                pos += length;
            }
        }

        lambda_t *lambda = &data->lambdas.funcs[lam];
        size_t    length = lambda->body.begin + lambda->body.length + 1 - pos;

        fwrite(source->data + pos, lambda->start - pos, 1, out);
        fprintf(out, "&lambda_%zu", lam);

        len -= length;
        pos += length;

        for (++lam; lam != data->lambdas.elements && data->lambdas.funcs[lam].start < pos; ++lam)
            ;
        proto = next_prototype_position(data, lam, proto);
    }
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
    qsort(data.positions.positions, data.positions.elements, sizeof(lambda_position_t), &compare_position);

    generate_marker(out, source->file, 1);

    generate_code(out, source, 0, source->length, &data, 0);

    for (size_t i = 0; i < data.lambdas.elements; i++) {
        lambda_t *lambda = &data.lambdas.funcs[i];
        generate_begin(out, source, &data.lambdas, i);
        fprintf(out, "\n#line %zu\n", lambda->body_line);
        generate_sliced(out, source->data, lambda->body.begin, lambda->body.length + 1, &data, i + 1);
    }

    /* there are cases where we get no newline at the end of the file */
    fprintf(out, "\n");

    lambda_vector_destroy(&data.lambdas);
    lambda_vector_destroy(&data.positions);
}

static void usage(const char *prog, FILE *out) {
    fprintf(out, "usage: %s <file>\n", prog);
}

static void version(FILE *out) {
    fprintf(out, "lambdapp 0.1\n");
}

int main(int argc, char **argv) {
    lambda_source_t source;
    const char *file = NULL;

    int i = 1;
    for (; i != argc; ++i) {
        if (!strcmp(argv[i], "--")) {
            ++i;
            break;
        }
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0], stdout);
            return 0;
        }
        if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
            version(stdout);
            return 0;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unrecognized option: %s\n", argv[0], argv[i]);
            usage(argv[0], stderr);
            return 1;
        }
        if (file) {
            fprintf(stderr, "%s: only 1 file allowed\n", argv[0]);
            usage(argv[0], stderr);
            return 1;
        }
        file = argv[i];
    }
    if (!file && i != argc)
        file = argv[i++];
    if (i != argc) {
        fprintf(stderr, "%s: only 1 file allowed\n", argv[0]);
        usage(argv[0], stderr);
        return 1;
    }

    if (!file) {
        usage(argv[0], stderr);
        return 1;
    }

    if (!parse_open(&source, file)) {
        fprintf(stderr, "failed to open file %s %s\n", file, strerror(errno));
        return 1;
    }

    generate(stdout, &source);
    parse_close(&source);

    return 0;
}
