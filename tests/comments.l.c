/* multi
 * line
 * comment
 *
 *
 */


//
// single
// line
// comment
//

#include <stdio.h>

void for_range(int start, int afterend, void (*func)(int)) {
    int dir = start < afterend ? 1 : -1;
    for (int i = start; i != afterend; i += dir)
        func(i);
}

int main(int argc, char **argv) {
    for_range(5, 10, lambda void(int i) { printf("%i\n", i); });
    for_range(10, 5, lambda void(int i) { printf("%i\n", i); });
    return 0;
}
