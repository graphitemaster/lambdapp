#include <stdio.h>

int main(int argc, char **argv) {
    printf("%i\n",
        lambda struct foo { int x; } (int arg) {
            struct foo f;
            f.x = arg;
            return f;
        }(3).x);
    return 0;
}
/* OUTPUT:
3
*/
