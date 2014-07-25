#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include <sys/types.h>
#include <dirent.h>

#define PP_ARRAY_COUNT(ARRAY) \
    (sizeof((ARRAY))/sizeof(*(ARRAY)))
#define PP_ARRAY_FOR(NAME, ARRAY) \
    for (size_t NAME = 0; NAME < PP_ARRAY_COUNT(ARRAY); NAME++)

static void lcc_usage(const char *app) {
    fprintf(stderr, "%s usage: [cc options]\n", app);
}

static void lcc_error(const char *message,  ...) {
    fprintf(stderr, "error: ");
    va_list va;
    va_start(va, message);
    vfprintf(stderr, message, va);
    fprintf(stderr, "\n");
    va_end(va);
}

typedef struct {
    const char *file;
    size_t      index;
    bool        cpp;
} lcc_source_t;

typedef struct {
    const char *output;
    size_t      index;
    bool        aout;
} lcc_output_t;

typedef struct {
    char   *buffer;
    size_t  used;
    size_t  allocated;
} lcc_string_t;

static bool lcc_string_init(lcc_string_t *string) {
    if (!(string->buffer = malloc(64)))
        return false;
    string->buffer[0] = '\0';
    string->used      = 1; /* always use the null byte */
    string->allocated = 64;
    return 1;
}

static bool lcc_string_resize(lcc_string_t *string) {
    size_t request = string->allocated * 2;
    void  *attempt = realloc(string->buffer, request);
    if (!attempt)
        return false;
    string->allocated = request;
    string->buffer    = attempt;
    return true;
}

static bool lcc_string_appendf(lcc_string_t *string, const char *message, ...) {
    va_list va1, va2;
    va_start(va1, message);
    va_copy(va2, va1); /* Copy the list */
    size_t count = vsnprintf(NULL, 0, message, va1);
    while (string->used + count >= string->allocated) {
        if (!lcc_string_resize(string))
            return false;
    }
    va_end(va1);

    va_start(va2, message);
    vsnprintf(string->buffer + string->used - 1, count + 1, message, va2);
    va_end(va2);
    string->used += count;
    return true;
}

static void lcc_string_destroy(lcc_string_t *string) {
    free(string->buffer);
}

static const char *lcc_lambdapp_find(void) {
    char *search;
    if ((search = getenv("LAMBDA_PP")))
        return search;

    static const char *bins[] = {
        ".", /* Try reliative to ourselfs as well */
        "/bin",
        "/usr/bin",
        
        /* When lambdapp is included as a submodule in a project */
        "lambdapp"
    };

    PP_ARRAY_FOR(bin, bins) {
        DIR *dir = opendir(bins[bin]);
        if (!dir)
            continue;

        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (entry->d_type != DT_REG && entry->d_type != DT_LNK)
                continue;

            if (!strcmp(entry->d_name, "lambda-pp")) {
                closedir(dir);
                return bins[bin];
            }
        }
        closedir(dir);
    }
    return NULL;
}

static const char *lcc_compiler_find(void) {
    /* Try enviroment variables first */
    char *search;
    if ((search = getenv("CC")))
        return search;
    if ((search = getenv("CXX")))
        return search;

    /* Start searching toolchain directories */
    static const char *bins[] = {
        "/bin",
        "/usr/bin",
    };

    static const char *ccs[] = {
        "cc", "gcc", "clang", "pathcc", "tcc"
    };

    PP_ARRAY_FOR(bin, bins) {
        DIR *dir = opendir(bins[bin]);
        if (!dir)
            continue;

        PP_ARRAY_FOR(cc, ccs) {
            /* Scan the directory for one of the CCs */
            struct dirent *entry;
            while ((entry = readdir(dir))) {
                /* Ignore things which are not files */
                if (entry->d_type != DT_REG && entry->d_type != DT_LNK)
                    continue;
                if (!strcmp(entry->d_name, ccs[cc])) {
                    closedir(dir);
                    return ccs[cc];
                }
            }
        }
        closedir(dir);
    }

    return NULL;
}

static bool lcc_source_find(int argc, char **argv, lcc_source_t *source) {
    static const char *exts[] = {
        /* C file extensions */
        ".c",

        /* C++ file extensions */
        ".cc", ".cx", ".cxx", ".cpp",
    };

    for (int i = 0; i < argc; i++) {
        PP_ARRAY_FOR(ext, exts) {
            char *find = strstr(argv[i], exts[ext]);
            if (!find)
                continue;

            /* It could be named stupidly like foo.c.c so we scan the whole filename
             * until we reach the end (when strcmp will succeed).
             */
            while (find) {
                /* Make sure it's the end of the string */
                if (!strcmp(find, exts[ext])) {
                    source->index = i;
                    source->file  = argv[i];
                    source->cpp   = (ext >= 1); /* See table of sources above for when this is valid. */
                    return true;
                }
                find = strstr(find + strlen(exts[ext]), exts[ext]);
            }
        }
    }
    return false;
}

static bool lcc_output_find(int argc, char **argv, lcc_output_t *output) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o"))
            continue;
        if (i + 1 >= argc) /* expecting output */
            return false;
        output->index  = i;
        output->output = argv[i + 1];
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    argc--;
    argv++;

    if (!argc) {
        lcc_usage(argv[-1]);
        return 1;
    }

    const char *cc = lcc_compiler_find();
    if (!cc) {
        lcc_error("Couldn't find a compiler");
        return 1;
    }

    const char *lambdapp = lcc_lambdapp_find();
    if (!lambdapp) {
        lcc_error("Couldn't find lambda-pp");
        return 1;
    }

    /* Get the arguments */
    lcc_string_t args_before; /* before -o */
    lcc_string_t args_after;  /* after -o */
    if (!lcc_string_init(&args_before)) {
        lcc_error("Out of memory");
        return 1;
    }
    if (!lcc_string_init(&args_after)) {
        lcc_error("Out of memory");
        lcc_string_destroy(&args_before);
        return 1;
    }
    
    /* Find the source file */
    lcc_source_t source;
    if (!lcc_source_find(argc, argv, &source)) {
        /* If there isn't any source file on the command line it means
         * the compiler is being used to invoke the linker.
         */
        lcc_string_destroy(&args_after);
        lcc_string_appendf(&args_before, "%s", cc);
        for (int i = 0; i < argc; i++) {
            if (!lcc_string_appendf(&args_before, " %s", argv[i]))
                goto args_oom;
        }

        int attempt = system(args_before.buffer);
        lcc_string_destroy(&args_before);
        return attempt;
    }

    /* Find the output file */
    lcc_output_t output = { .aout = false };
    if (!lcc_output_find(argc, argv, &output)) {
        /* not found? default to a.out */
        output.output = "a.out";
        output.aout = true;
    }
    
    /* Stop at the -o */
    size_t stop = output.aout ? (size_t)argc : output.index;
    for (size_t i = 0; i < stop; i++) {
        /* Ignore the source file */
        if (i == source.index)
            continue;
        if (!lcc_string_appendf(&args_before, "%s ", argv[i]))
            goto args_oom;
    }
    /* Trim the trailing whitespace */
    if (args_before.used >= 2)
        args_before.buffer[args_before.used - 2] = '\0';
    
    /* Handle anythng after the -o */
    stop += 2; /* skip -o and <output> */
    size_t count = argc;
    if (stop != count) {
        for (size_t i = stop; i < count; i++) {
            if (!lcc_string_appendf(&args_after, "%s ", argv[i]))
                goto args_oom;
        }
    }
    /* Trim trailing whitespace */
    if (args_after.used >= 2)
        args_after.buffer[args_after.used - 2] = '\0';

    /* Build the shell call */
    lcc_string_t shell;
    if (!lcc_string_init(&shell))
        goto args_oom;

    const char *lang = source.cpp ? "c++" : "c";
    if (!lcc_string_appendf(&shell, "%s/lambda-pp %s | %s -x%s %s - -o %s %s",
        lambdapp, source.file, cc, lang, args_before.buffer, output.output, args_after.buffer))
            goto shell_oom;

    int attempt = 0;
#ifndef _NDEBUG
    /* Call the shell */
    attempt = system(shell.buffer);
#else
    printf("%s\n", shell.buffer);
#endif

    lcc_string_destroy(&shell);
    lcc_string_destroy(&args_before);
    lcc_string_destroy(&args_after);

    return attempt;

shell_oom:
    lcc_string_destroy(&shell);
args_oom:
    lcc_error("Out of memory");
    lcc_string_destroy(&args_before);
    lcc_string_destroy(&args_after);
    return 1;
}
