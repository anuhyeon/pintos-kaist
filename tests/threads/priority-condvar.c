/* Tests that cond_signal() wakes up the highest-priority thread
   waiting in cond_wait(). */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func priority_condvar_thread;
static struct lock lock;
static struct condition condition;

void
test_priority_condvar (void) 
{
  int i;
  
  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  lock_init (&lock);
  cond_init (&condition);

  thread_set_priority (PRI_MIN);
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 7) % 10 - 1;
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);
      thread_create (name, priority, priority_condvar_thread, NULL);
      
    }
 
  for (i = 0; i < 10; i++)  // 위 코드는 10번 반복하면서 lock을 획득하고 조건 변수에 신호를 보낸 후 lock을 해제. 이를 통해 각 스레드를 차례로 깨움.
    { 
      struct thread *t = thread_current();
      //printf("현재 실행중인 스레드: %s\n",t->name);
      lock_acquire (&lock); // 락을 획득하여 임계구역에 진입
      msg ("Signaling...");
      cond_signal (&condition, &lock); // 조건 변수에 신호를 보내 대기 중인 스레드 하나를 깨움
      lock_release (&lock); // 락을 해제하여 다른 스레드가 락을 획득할 수 있도록 함.
    }
    /*위 코드는 lock_acquire와 lock_release 함수를 사용하여 */
}
/*각 스레드가 실행하는 코드. 스레드는 시작 메시지를 출력하고 lock을 획득한 후 조건 변수에서 신호를 기다림. 신호를 받으면 깨어났다는 메시지를 출력하고 lock을 해제*/
static void
priority_condvar_thread (void *aux UNUSED) 
{
  msg ("Thread %s starting.", thread_name ()); // 스레드가 시작되었음을 알리는 메시지 출력
  lock_acquire (&lock);  // lock을 획득하여 크리티컬 섹션에 진입
  //printf("lock: %s\n",lock.holder->name);
  cond_wait (&condition, &lock); // 조건 변수에서 신호를 기다리며 lock을 해제하고 대기
  msg ("Thread %s woke up.", thread_name ()); // 신호를 받아 깨어났음을 알리는 메시지 출력
  lock_release (&lock); // lock을 해제하여 다른 스레드가 lock을 획득할 수 있도록 함
}

// 컨디션 웨이트랑 세마포 ㅇ=ㅞ이츠 둘다 바꾸는건데 cmp 이거는 세마에서는 쓸 수 있는데 컨디션 웨이트에서는 못써  그러면 또 다른 함수를 만들어야한다 cmp_cond 버전을 만들어야한다.