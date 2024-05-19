/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) { // 세마포를 초기화 하는 함수 인자로는 세마포 구조체와 사용가능한 자원의 수(value)를 받는다. 
	ASSERT (sema != NULL);

	sema->value = value; // 여기서 semaphore가 사용 가능이면 1 이상을 넣을 것이고 (현재는 1이 최대이다.) 아니라면 0을 넣을 것이다. -> 이진세마포 사용
	list_init (&sema->waiters);
}

//list_insert_ordered (&cond->waiters, &waiter.elem, sema_compare_priority, 0);

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. 
   
   프로그래밍에서 원자적 연산은 시작되면 
   중간에 다른 스레드나 프로세스에 의해 방해받지 않고 
   완전히 수행되는 연산을 말한다.

   원자적 연산은 그 실행 과정에서 
   어떠한 병렬 처리나 동시성 문제에서도 
   안전하게 동작하도록 보장한다.

   "세마포어의 값을 원자적으로 감소시킨다"는 것은 
   세마포어의 값을 감소시키는 작업이 시작되면, 
   그 작업이 완전히 완료될 때까지 다른 어떤 작업도 개입할 수 없음을 의미.
   
   */
void
sema_down (struct semaphore *sema) { // 세마포의 down 연산(세마포 값을 감소시키는 것 또한 원자적으로 수행해야하므로 함수를 만든 것): 세마포의 값이 양수가 될 때까지 대기한 후, 이를 원자적으로 감소시키는 역할
	enum intr_level old_level; // 인터럽트 이전 상태(활성or 비활성)를 저장하는 변수

	ASSERT (sema != NULL); // 세마포 포인터가 널이 아님을 확인
	ASSERT (!intr_context ()); // 현재 코드가 인터럽트 핸들러 내부에서 호출되지 않았음을 확인

	old_level = intr_disable (); // 현재 인터럽트 상태를 비활성화하고 이전 상태를 old_level에 저장 -> 코드 실행동안 인터럽트가 발생하지 않도록 보장하기 위해서
	while (sema->value == 0) {  // 세마포 값이 0인 경우
		//list_push_back (&sema->waiters, &thread_current ()->elem); // 현재 스레드를  waiter리스트에 삽입 
		list_insert_ordered(&sema->waiters,&thread_current()->elem,cmp_priority,NULL);
      thread_block (); // 현재스레드를 대기상태로 전환
	}
	sema->value--; // 세마포의 값이 0보다 크면 이 구문은 세마포의 값을 원자적으로 1감소 시킨다.
	intr_set_level (old_level); // 인터럽트 상태를 이전상태로 다시 복원
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) { // 이는 세마포의 값이 증가함에 따라 임계 구역에 진입할 수 있는 권한을 얻은 스레드를 깨우는 과정 -> 
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {//세마포의 대기자 목록이 비어있지 않은 경우를 검사
		list_sort(&sema->waiters, cmp_priority, NULL);    //sema_up 에서는 waiters 리스트에 있던 동안 우선순위에 변경이 생겼을 수도 있으므로 waiters 리스트를 내림차순으로 정렬 -> 예를 들어, 새로 생성된 스레드의 우선순위가 높아 현재 락을 기다리고 있는 스레드들에게 우선순위를 donation하게 되면  waiter안의 스레드들의 우선순위가 변동됨.-> 체이닝 상황에서 발생
      thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem)); // 만약 대기자가 있다면 list_pop_front (&sema->waiters)를 통해 대기 목록에서 가장 앞에 있는 스레드를 제거하고, thread_unblock() 함수를 호출하여 해당 스레드의 상태를 대기에서 실행 가능 상태로 변경한다.  
   }
   sema->value++; // 세마포의 값을 1 증가시킴. sema_down 이는 추가적인 스레드가 임계 구역에 진입할 수 있음을 의미하며, up 연산의 핵심 부분 -> sema_down()안의 while문에서 block되어있던 스레드가 sema_up()에서 sema ++으로 일어날 때, thread는 sema_down()의thread_block(); 부터 시작한다. 그러나 이 때, (별일이 없다면) sema->value 는 1이기 때문에 semaphore를 획득할 수 있다
	intr_set_level (old_level); // max_priority_in_ready_preemption함수 안에 thread_yield함수 안에 intr_disable(), intr_set_level() 함수가 있기 때문에 해당 코드 밖에서 Preempt함수 선언해주어야함.
   max_priority_in_ready_preemption(); // unblock 된 스레드가 running 스레드보다 우선순위가 높을 수 있으므로 preemption함수 실행하여 CPU선점 시도, CPU를 실행중인 스레드의 우선순위가 바뀌거나 ready_list에 새로운 스레드가 들어오는 경우 함수 호출하면 됨.

}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock.

    lock은 semaphore와 lock->horder를 가지고 있는 구조체이다.
	lock->holder는 thread임.
	semaphore는 lock의 핵심 메커니즘으로, 이를 통해 어느 스레드가 lock을 가질 수 있는지를 결정한다.
	semaphore의 value라는 값이 1 이상이면, 이는 lock을 획득할 수 있다는 것을 의미.
	반면에 value가 0 이하면, 이는 모든 lock이 이미 다른 스레드에 의해 보유되고 있음을 나타냄.

	어떤 스레드가 lock을 성공적으로 획득하면,
	semaphore의 value는 감소하고,lock->holder는 해당 스레드를 가리키게 됨.
	그 스레드가 나중에 lock을 릴리즈하면, semaphore의 value는 다시 증가하고,
	lock->holder는 NULL로 설정되어 다른 스레드가 lock을 얻을 수 있게 됨.

	*/
void
lock_init (struct lock *lock) { // 주어진 락 구조체를 초기화하여 사용할 준비를 진행
	ASSERT (lock != NULL); //lock이 null이 아닌지 확인

	lock->holder = NULL; //lock->holder는 현재 lock을 보유하고 있는 스레드를 나타내며 초기엔 아무도 lock을 보유하고있지 않으므로 NULL로 설정됨.
	sema_init (&lock->semaphore, 1); // 락 내부에 있는 세마포를 초기화 초기 값은 1로 설정
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. 

   LOCK을 획득합니다. 필요하다면 사용 가능해질 때까지 대기합니다.
   이때, 현재 스레드에 의해 이미 잠금이 걸려 있으면 안 됩니다.

   이 함수는 대기 상태가 될 수 있으므로,
   인터럽트 핸들러 내에서는 호출되어서는 안 됩니다.
   이 함수는 인터럽트가 비활성화된 상태로 호출될 수 있지만,
   대기가 필요한 경우 인터럽트는 다시 활성화됩니다.
   
   스레드가 lock을 요청할 때 실행되는데 lock을 현재 점유하고 있는 스레드가 없으면 lock을 바로 획득할 수 있지만
   누군가 점유하고 있다면 자신의priority(자신이 더 높다면)를 양도해서 lock을 점유하고 있는 스레드가 우선적으로
   CPU를 사용하도록하여 lock을 빠르게 반환하도록하고 해야함. 그래야 자신이 우선순위에 맞게 CPU사용가능.
   */
void
lock_acquire (struct lock *lock) { // 는 스레드가 주어진 lock을 안전하게 획득할 수 있도록 도와주며, 필요한 경우 lock이 사용 가능해질 때까지 스레드를 대기 상태로 전환하는 역할 
	ASSERT (lock != NULL); // 안전 체크
	ASSERT (!intr_context()); // 현재 코드가 인터럽트 내부에서 실행되지 않음을 확인. lock_acquire 함수는 스레드가 대기 상태로 전환될 수 있기 때문에, 인터럽트 핸들러에서는 호출될 수 없음. 인터럽트 핸들러 내에서는 스레드를 대기 상태로 만들 수 없기 때문
	ASSERT (!lock_held_by_current_thread (lock)); // 이 구문은 현재 스레드가 이미 lock을 보유하고 있지 않은지 확인 -> 재귀적 락 획득을 방지하기 위함, 이미 lock을 보유한 스레드가 다시 그 lock을 획득하려고 시도하는 경우를 막음.
  
   /*-----------------------------------project1 BSD mlfqs--------------------------------------*/
   if (thread_mlfqs) {
    sema_down (&lock->semaphore);
    lock->holder = thread_current ();
    return ;
  }
   /*-----------------------------------project1 BSD mlfqs--------------------------------------*/
   
   /*------------------------project1 priority scheduling Donation------------------------------*/
   struct thread *cur = thread_current();

   if (lock->holder != NULL) { // lock을 누군가 점유하고 있으면
      cur->wait_on_lock = lock; // 현재 스레드는 나중에 사용하기 위해서 락을 저장
      list_insert_ordered (&lock->holder->donations, &cur->donation_elem, thread_cmp_donate_priority, NULL);
      donate_priority();
  }
   /*------------------------project1 priority scheduling Donation------------------------------*/
   sema_down (&lock->semaphore); //sema_down을 기점으로 이전은 현재스레드가 lock을 얻기전, 이후는 lock을 얻은 후. lock 내부 세마포에 대해서 sema_down함수를 호출함. 이함수는 세마포의 value를 감소 시키려고함. 만약 value가 0보다 크다면, 이는 lock을 성공적으로 획득할 수 있음을 의미하며 value를 감소시키고 진행합니다. 만약 value가 0이라면, 이는 모든 lock이 이미 다른 스레드에 의해 보유되고 있음을 의미하고 현재 스레드는 lock이 사용 가능해질 때까지 대기 상태로 전환됨
	cur->wait_on_lock = NULL;
   lock->holder = thread_current();//thread_current(); // 재 스레드를 lock의 보유자로 설정
   //printf("lock holder is %s\n",lock->holder->name);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. 
   lock_release함수는 현재 스레드가 소유하고 있는 lock을 해제하는 과정을 구현
   lock_release 함수는 현재 스레드가 소유한 lock을 안전하게 해제하고,
   해당 lock에 대기 중인 다른 스레드가 있다면, 그 스레드가 lock을 획득할 수 있도록 한다.
   */
void
lock_release (struct lock *lock) { // 현재 스레드가 소유하고 있는 락을 해제하는 코드
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock)); // 현재 스레드가 해당 LOCK을 실제로 소유하고 있는지확인(현재 스레드가 LOCK을 해제할 권한이 있는지를 검증)
   /*----------------------------------project1 BSD scheduling---------------------------------------*/
   lock->holder = NULL;
   if (thread_mlfqs) {
    sema_up (&lock->semaphore);
    return ;
  }
   /*----------------------------------project1 BSD scheduling---------------------------------------*/

   
   /*------------------------project1 priority scheduling Donation------------------------------*/
   remove_with_lock (lock);
   reset_priority();
   /*------------------------project1 priority scheduling Donation------------------------------*/

	//lock->holder = NULL; // 현재 스레드가 더 이상 lock을 소유하지 않음을 나타내기 위해, lock의 소유자(holder)를 NULL로 설정. 이것은 lock이 이제 다른 스레드에 의해 획득될 수 있음을 의미
   sema_up (&lock->semaphore); //lock의 내부 semaphore에 대해 sema_up 함수를 호출. sema_up 함수는 semaphore의 value를 증가시키고, 만약 대기 중인 스레드가 있다면, 하나의 스레드를 대기 상태에서 깨워 lock을 획득하도록 함. 이 과정은 lock이 해제되었을 때, 대기 중인 다른 스레드가 lock을 획득할 수 있도록 보장.
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. 
   
   이 함수는 특정 lock을 자동으로 해제하고,
   주어진 조건 변수(COND)가 다른 코드에 의해 신호를 받을 때까지 대기한 다음,
   조건이 신호를 받으면 lock을 다시 획득하기 전에 반환
   
   대기 중인 스레드에 대해 별도의 세마포어를 제공한다는 것은, 
   각각의 스레드가 조건 변수에 의해 대기 상태로 들어갈 때, 
   그 스레드만의 고유한 세마포어를 가지고 있다는 의미

   */
void
cond_wait (struct condition *cond, struct lock *lock) {// 
	struct semaphore_elem waiter; // 조건 변수 대기자(세마포) 생성 , 대기 중인 스레드에 대해 별도의 세마포어를 제공

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0); // 대기자 세마포를 0으로 초기화하며, 이 세마포에 대해 sema_down이 호출되면 스레드가 즉시 대기 상태가 되도록함.
   //list_push_back (&cond->waiters, &waiter.elem); // 생성된 대기자를 조건 변수의 대기자 목록에 추가
   list_insert_ordered (&cond->waiters, &waiter.elem, cmp_sema_priority, NULL); // 어차피 빈 깡통 waiter.elem에는 아무론 값도 들어 있지 않으므로 그냥 waiter.elem이라는 형태만 넣어주면됨 여기에 값은 나중에 넣을 것임.
   lock_release (lock); // 주어진 락을 해제 
	sema_down (&waiter.semaphore); //인자로 들어간 친구는 cond->semaphore라고 생각하면됨. 현재 스레드를 대기 상태로 전환, 조건변수가 신호를 받을 때 까지 여기서 대기
	lock_acquire (lock); // 조건 변수가 신호를 받으면 현재 스레드가 다시lock을 획득 -> 이는 함수가 반환되기 전에 lock이 재획득 되어야함을 의미
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. 
   
   이 함수는 조건 변수(COND)를 대기 중인 스레드 중 하나에게 신호를 보내어 
   대기 상태에서 깨우는 역할을 한다. 
   이 함수를 호출할 때는 반드시 LOCK이 잡혀 있어야 한다.
   즉, 이 함수는 특정 조건이 충족되었을 때 대기 중인 스레드 중 하나를 깨우고자 할 때 사용
   
   */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL); // 널이면 경고
	ASSERT (lock != NULL);
	ASSERT (!intr_context ()); // 이 함수가 인터럽트 핸들러 내에서 호출되지 않도록 함. 인터럽트 핸들러에서는 락을 획득할 수 없으므로 조건 변수를 sinal할수 없음.
	ASSERT (lock_held_by_current_thread (lock)); // 현재 스레드가 락을 소유하고 있지 않으면 경고: 현재 스레드가 lock을 소유하고 있는지 확인, 이 함수를 호출하기 전에 lock을 현재 스레드가 소유하고 있어야함.
   // ist_entry는 list_elem 구조체를 포함하는 semaphore_elem 구조체로 변환
	if (!list_empty (&cond->waiters)) {// 대기자 목록이 비어있지 않은 경우, 즉 대기 중인 스레드가 있는 경우에만 조건문 실행
		list_sort (&cond->waiters, cmp_sema_priority, NULL);
      sema_up (&list_entry (list_pop_front (&cond->waiters),struct semaphore_elem, elem)->semaphore);
   } // 대기목록의 맨 앞에서 대기 중인 스레드를 제거하고, 해당 스레드의 세마포에 sema_up을 호출하여 대기 상태에서 깨움->list_pop_front는 대기 목록에서 첫 번째 요소를 제거하고 list_entry는 list_elem 구조체를 포함하는 semaphore_elem구조체로 변환하고 sema_up호출을 통해 대기 중인 스레드를 깨움
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler.
   
   조건 변수 cond를 기다리고 있는 모든 스레드를 깨우는 함수인 cond_broadcast의 구현

   while 루프 내에서는 cond_signal 함수를 호출하여 대기 중인 스레드 중 하나를 깨우는 작업을 수행한다. 
   이 과정은 모든 대기 중인 스레드가 깨어날 때까지 반복된다. 
   즉, cond->waiters 리스트가 비어있을 때까지 계속해서 대기 중인 스레드를 하나씩 깨우는 것.

   함수 주석에서 언급된 바와 같이, 
   인터럽트 핸들러 내에서는 락을 획득할 수 없기 때문에, 
   이러한 환경에서는 cond_broadcast 함수를 사용해서는 안 됨. 
   이는 인터럽트 핸들러가 가능한 한 빠르게 실행되어야 하며, 
   락을 기다리는 동안 발생할 수 있는 지연을 피해야 하기 때문임.

   요약하자면, cond_broadcast 함수는 특정 조건 변수를 기다리고 있는 모든 스레드를 깨우기 위해 사용된다.

    */
void
cond_broadcast (struct condition *cond, struct lock *lock) {  // 깨우고자 하는 조건 변수를 가리키는 포인터(condition *cond)를 인자로 받음, 조건변수를 보호하는 락을 가리키는 포인터(lock *lock) 
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
   /**/
	while (!list_empty (&cond->waiters)) // waiters 리스트가 빌때 까지 계속 반복문 돌림,  cond->waiters는 이 조건 변수를 기다리고 있는 스레드들의 리스트를 가리킴
		cond_signal (cond, lock);
}

//cmp_priority와 다른 점은 이번에는 입력받는 파라미터가 스레드가 아니라 semaphore_elem.elem 로 들어오기 때문에 thread_compare_priority 함수는 사용하지 못하고, semaphore_elem 가 나타내는 세마포의 waiters 리스트의 맨 앞 thread 끼리 priority 를 비교하는 함수가 필요하다. (semaphore_elem 구조체는 synch.c 에 선언되어 있다.)
bool cmp_sema_priority (const struct list_elem *l, const struct list_elem *s, void *aux UNUSED)
{
	struct semaphore_elem *l_sema = list_entry(l, struct semaphore_elem, elem);
	struct semaphore_elem *s_sema = list_entry(s, struct semaphore_elem, elem);

	struct list *waiter_l_sema = &(l_sema->semaphore.waiters);
	struct list *waiter_s_sema = &(s_sema->semaphore.waiters);
   //아래는 빈 깡통의 elem 값을 확인하기 위한 코드
   //printf("%d  ,  %d\n",list_entry (list_begin (waiter_l_sema), struct thread, elem)->priority , list_entry (list_begin (waiter_s_sema), struct thread, elem)->priority);
	return list_entry (list_begin (waiter_l_sema), struct thread, elem)->priority > list_entry (list_begin (waiter_s_sema), struct thread, elem)->priority;
}