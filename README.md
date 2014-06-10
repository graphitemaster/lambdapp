### Synposis
LambdaPP is a preprocessor for giving you anonymous functions in C.

### Examples
```
// for an example the table consists of a string keyed (room) of occupants
// stored in a linked list.
hashtable_t *table;
hashtable_foreach(table,
    lambda void(list_t *list) {
        list_foreach(list,
            lambda void(const char *occupant) {
                printf(">> %s\n", occupant);
            }
        );
    }
);
```

Closures are not supported by this system. It's important to note these are not
nested functions or blocks, for information on these please see the following
links.

[_Nested functions_](https://gcc.gnu.org/onlinedocs/gcc/Nested-Functions.html)

[_Blocks_](http://clang.llvm.org/docs/Block-ABI-Apple.html)

This is a source translation that produces global functions and replaces instances
of the lambda with the literal.

### How it works
Given a lambda, a static function is created. The scope which implements the
lambda is replaced with a reference to the static function by taking its' address.

#### Example
```
(lambda void(void) { printf("Hello world"); })();
```

Would be translated to
```
static void lambda_0(void);
(&lambda_0)();
static void lambda_0(void) { printf("Hello world"); }
```

To better see how it works, here's the original example expanded
```
hashtable_t *table;
static void lambda_0(list_t *list);
hashtable_foreach(table, &lambda_0);
static void lambda_1(const char *occupant);
static void lambda_0(list_t *list) {
    list_foreach(list, &lambda_1);
}
static void lambda_1(const char *occupant) {
    printf(">> %s\n", occupant);
}
```

### Diagnostics
LambdaPP inserts `#file` and `#line` directives into the source code such that
compiler diagnostics will still work.
