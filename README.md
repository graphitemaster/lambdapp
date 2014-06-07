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
nested functions or blocks.

[_Nested functions_](https://gcc.gnu.org/onlinedocs/gcc/Nested-Functions.html)
[_Blocks_](http://clang.llvm.org/docs/Block-ABI-Apple.html)

This is a source translation that produces global functions and replaces instances
of the lambda with the literal.

### How it works
Given a lambda, a global function is created. The scope which implements the
lambda is replaced with a reference to the global function by taking its' address.

#### Example
```
(lambda void(void) { printf("Hello world"); })();
```

Would be translated to
```
({ void lambda_0(void); &lambda_0 })();
void lambda_0(void) { printf("Hello world"); }
```

Take note that we do utilize compound statement expressions since we don't know
where to put the prototype for the lambda when it's referenced.
