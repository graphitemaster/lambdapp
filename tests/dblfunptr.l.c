#include <stdio.h>

int three(void) { return 3; }
int (*threefunc(void))(void) { return &three; }

int main(int argc, char **argv) {
    printf("%i\n",
        lambda int (*(*(int x))(void))(void) {
            (void)x;
            return &threefunc;
        }(42)()());
    return 0;
}
/* OUTPUT:
3
*/
