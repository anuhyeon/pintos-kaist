/* The main thread acquires a lock.  Then it creates two
   higher-priority threads that block acquiring the lock, causing
   them to donate their priorities to the main thread.  When the
   main thread releases the lock, the other threads should
   acquire it in priority order.

   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by Matt Franklin <startled@leland.stanford.edu>,
   Greg Hutchins <gmh@leland.stanford.edu>, Yu Ping Hu
   <yph@cs.stanford.edu>.  Modified by arens. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func acquire1_thread_func;
static thread_func acquire2_thread_func;

void
test_priority_donate_one (void) 
{
  struct lock lock;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs); // 이 테스트는 멀티 레벨 피드백 큐 스켖줄러가 활성화 되지 않았을 경우만 실행, 이유는 MLFGS에서는 우선순위가 동적으로 조정되기 때문

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);  // 31스레드 메인스레드

  lock_init (&lock); 
  lock_acquire (&lock); // 31 메인스레드가 락 획득
  thread_create ("acquire1", PRI_DEFAULT + 1, acquire1_thread_func, &lock); // 31 메인스레드가 32 스레드 생성하고 우선순위로 CPU 점유해서 31스레드에서 acquire1_thread_func을 실행 하지만 락을 메인이 사용하고 있어서 acquire1_thread_func내 lock_acquire (lock)내 sema_down()내 while문안에서 waiter에 들어가고, block상태가 되어버림.   "acquire1"이라는 이름의 새 스레드를 생성하고, 이 스레드에게 lock을 인자로 전달. 이 스레드의 우선순위는 기본 우선순위보다 1 높음.
  msg ("This thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ()); // 
  //printf("현재 실행중인 스레드: %s\n", thread_current()->name);
  thread_create ("acquire2", PRI_DEFAULT + 2, acquire2_thread_func, &lock);
  //printf("현재 실행중인 스레드: %s\n", thread_current()->name);

  msg ("This thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());
  lock_release (&lock);
  msg ("acquire2, acquire1 must already have finished, in that order.");
  msg ("This should be the last line before finishing this test.");
}

static void
acquire1_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("acquire1: got the lock");
  lock_release (lock);
  msg ("acquire1: done");
}

static void
acquire2_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("acquire2: got the lock");
  lock_release (lock);
  msg ("acquire2: done");
}
