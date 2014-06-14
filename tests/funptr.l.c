#include <stdio.h>

int three(void) { return 3; }

int main(int argc, char **argv) {
    printf("%i\n",
        lambda int (*(int x))(void) {
            (void)x;
            return &three;
        }(42)());
    return 0;
}
/* OUTPUT:
3
*/
