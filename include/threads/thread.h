#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 * 각 struct thread는 자신만의 4kb 페이지 안에 저장.
 * 여기서 '커널 스택'이란, 스레드가 함수 호출과 같은 작업을 수행할 때 필요한 임시 데이터를 저장하는 공간
 * 커널 스택은 주로 프로그램의 실행 상태, 로컬 변수 등을 저장하는 데 사용
 * 페이지의 구조: 각 스레드는 4kB 크기의 페이지를 할당받음.
 * 이 페이지 안에서, 스레드의 정보를 담고 있는 struct thread 구조체는 페이지의 가장 아래에 존재(0 오프셋 부근).
 * 
 * 커널 스택의 위치와 성장 방향: struct thread 구조체 위의 페이지 나머지 공간은 커널 스택을 위해 예약.
 * 커널 스택은 페이지의 상단(4kB 오프셋)에서 시작하여 아래쪽으로(0 오프셋 방향으로) 성장.
 * 즉, 스택에 데이터가 추가될수록, 스택의 'top'은 페이지의 아래쪽으로 이동.
 * 이렇게 디자인하는 이유는, 커널 스택이 너무 커져서 struct thread 구조체와 충돌하는 것을 방지하기 위함임. 스택이 너무 커지면 struct thread 구조체를 덮어쓸 수 있고, 이는 시스템 오류로 이어질 수 있게됨.
 * 
 * 간단하게 말하자면, 각 스레드는 자신만의 메모리 공간(4kB 페이지)를 갖고, 이 공간 안에서 스레드 정보(struct thread)와 커널 스택이 분리되어 관리된다는 것을 이해할 것
 * 
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 * 
 * 		struct tread의 크기 제한 -> struct thread의 크기가 너무 크면 커널 스택을 위한 충분한 공간이 없어진다
 * 		기본적인 struct thread의 크기는 1KB 이하로 유지해야함
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 * 
 * 		커널 스택의 크기 제한 -> 마찬가지로 커널 스택이 커지면 스레드 상태를 손상 시킬 수 있음.
 * 		따라서 커널 함수는 큰 구조체난 배열을 비정적 지역 변수로 할당하면 안되고 malloc() 이나 palloc_get_page()와 같은 동적할당을 사용할 것
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. 
 * 
 * 택 오버플로우나 struct thread의 크기 문제는 주로 thread_current()에서의 단언문(assertion) 실패로 처음 나타남.
 *  이 함수는 실행 중인 스레드의 struct thread에 있는 magic 멤버가 THREAD_MAGIC으로 설정되어 있는지를 확인.
 *  스택 오버플로우는 보통 이 값을 변경시켜 단언문을 트리거함.
 * 
 * 
 * 
 * */

/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. 
 * 
 * 
 * elem 멤버는 실행 대기 큐(run queue)의 요소로, 또는 세마포어(semaphore) 대기 목록의 요소로 사용될 수 있음.
 *  이 두 가지 용도는 서로 배타적이기 때문에 가능: 준비 상태(ready state)에 있는 스레드만 실행 대기 큐에 있을 수 있고,
 *  차단 상태(blocked state)에 있는 스레드만 세마포어 대기 목록에 있을 수 있음.
 * 
 * 
 * 
 * */
struct thread { // 각 스레드나 사용자 프로세스를 관리하기 위해 thread 구조체 사용, thread 구조체는 스레드의 상태, 스택, 그리고 다양한 관리 정보를 포함
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	int64_t wakeup_tick; // 스레드마다 일어나야 하는 시간에 대한 정보를 담고 있어야하는데 이르 wakeup변수에 저장 

	/*------------PROJECT1 priority-inversion(donation)-----------*/
	// 각 스레드가 양도 받은 내역을 관리할 수 있도록 thread의 구조체를 변경해 줄 필요가 있음.
	int init_priority; // 스레드가 priority를 양도받았다가 다시 반납할 때 원래의 priority를 복원할 수 있도록 고유의 priority 값을 저장하는 변수

	struct lock *wait_on_lock; // 스레드가 현재 얻기 위해 기다리고 있는 lock 으로 스레드는 이 lock 이 release 되기를 기다림.
	struct list donations;// 자신에게 priority를 기부해준 스레드들의 리스트(multiple donation을 고려하기 위해 사용)
	struct list_elem donation_elem; // donation 리스트를 관리하기 위한 element 로 thread 구조체의 그냥 elem 과 구분하여 사용할 것(multiple donation을 고려하기 위해 사용)
	// donations는 리스트를 관리하는 구조체이고, donation_elem은 리스트의 각 요소를 관리하는 구조체
	/*
	t->donation_elem: struct thread 구조체 내의 멤버로, 
	이 스레드가 다른 스레드의 donations 리스트에 들어갈 때 사용된다. 
	이는 이 스레드가 기부자(donor)로서 기부 정보로 사용되는 리스트 요소이다.

	t->donations: struct thread 구조체 내의 멤버로, 
	이 스레드가 받은 우선 순위 기부들을 추적하는 리스트입. 
	이는 이 스레드가 수혜자(donee)로서, 다른 스레드들이 자신에게 기부한 우선 순위 정보를 저장.
	
	즉, A라는 스레드가 B스레드에게 우선순위를 양보를 하면
	B스레드의 donations에는 A라는 스레드의 A스레드의 donation_elem 와 다른 기부자들의 donations_elem이 정렬되어 저장되어있음.
	*/
#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

//project_1 (Alam_Clock)
void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);
int64_t get_min_tick_to_awake(void);
void update_min_tick_to_awake(int64_t ticks);
//project_1 (Priority_Scheduling)
void max_priority_in_ready_preemption(void); //CPU를 실행중인 스레드의 우선순위가 바뀌거나 ready_list에 새로운 스레드가 들어오는 경우 함수 호출하면 됨. 현재 수행중인 스레드와 ready_list에 있는 스레드 중 우선순위가 가장 높은 스레드를 비교하여 스케줄링
bool cmp_priority(const struct list_elem *new_elem,const struct list_elem *e, void *aux UNUSED); // 새롭게 들어온 new_elem스레드와 기존에 있던 e스레드와 우선순위 비교하여 true / false 반환
//project_1 (Priority_Scheduling) - donation
void donate_priority (void); 
void reset_priority (void); // donations리스트 sort후 가장 높은 스레드의 우선순위로 락홀더의 우선순위를 바꾸어줌
void remove_with_lock (struct lock *lock); // 락을 해제하면 해당 락을 
bool thread_cmp_donate_priority (const struct list_elem *l, const struct list_elem *s, void *aux UNUSED);
bool cmp_sema_priority (const struct list_elem *l, const struct list_elem *s, void *aux);


int thread_get_priority (void); // 실행중인 스레드의 우선순위를 반환하는 함수
void thread_set_priority (int); // 실행중인 스레드의 우선순위를 변경하는 함수

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
