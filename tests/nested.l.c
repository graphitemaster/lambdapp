#include <stdio.h>

void for_range(int start, int afterend, void (*func)(int)) {
  int dir = start < afterend ? 1 : -1;
  for (int i = start; i != afterend; i += dir)
    func(i);
}

int main(int argc, char **argv) {
  for_range(5, 7, lambda void(int i) {
      printf("%i\n", i);
      for_range(60, 62, lambda void(int i) {
          printf(">> %i\n", i);
      });
      for_range(50, 52, lambda void(int i) {
          printf(">> %i\n", i);
          for_range(500, 502, lambda void(int i) {
              printf(">>>> %i\n", i);
          });
      });
  });
  return 0;
}

/* OUTPUT:
5
>> 60
>> 61
>> 50
>>>> 500
>>>> 501
>> 51
>>>> 500
>>>> 501
6
>> 60
>> 61
>> 50
>>>> 500
>>>> 501
>> 51
>>>> 500
>>>> 501
*/
