/* Prints the command-line arguments.
   This program is used for all of the args-* tests.  Grading is
   done differently for each of the args-* tests based on the
   output. */

#include "tests/lib.h"

int
main (int argc, char *argv[]) 
{
  int i;

  test_name = "args";

  if (((unsigned long long) argv & 7) != 0)
    msg ("argv and stack must be word-aligned, actually %p", argv);
  msg ("begin");
  msg ("argc = %d", argc);
  for (i = 0; i <= argc; i++)
    if (argv[i] != NULL)
      msg ("argv[%d] = '%s'", i, argv[i]);
    else
      msg ("argv[%d] = null", i);
  msg ("end");

    return 0; // 이 '0'이라는 친구는 exit()함수의 status 인자로 들어가게됨. 그럼 main thread가 0을 return 하면 정상 종료가 되었다는 의미이고 exit()함수에 0을 인자로 주면 exit함수 내에서 이 인자를 가지고 정상종료되었다는 코드를 구현함
}
