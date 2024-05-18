#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/*Define Sleep Queue 추가*/ 
static struct list sleep_list; // block 상태가 된 스레드를 따로 관리하는 sleep 상태의 스레들로만 이루어진 리스트 선언
static int64_t min_tick_to_awake; //  sleep_list에서 대기중인 스레드들의 wakeup_tick 값 중 최솟값을 저장하기 위한 변수 추가


/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */



/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);


/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init (&sleep_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority, 
		thread_func *function, void *aux) { // 새로운 스레드 생성하는 함수-> 스레드의 이름(name), 우선순위(priority), 실행할 함수(function), 그리고 해당 함수에 전달할 인자(aux)를 매개변수로 받음
	struct thread *t; // 스레드 구조체 포인터 선언
	tid_t tid; // 스레드의 id

	ASSERT (function != NULL); // function이 NULL이 아닌지 확인

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO); // palloc_get_page(PAL_ZERO)를 통해 스레드 구조체를 위한 메모리를 할당. PAL_ZERO 옵션은 할당된 메모리를 0으로 초기화. 
	if (t == NULL) //메모리 할당이 실패하면
		return TID_ERROR; // TID_ERROR를 반환

	/* Initialize thread. */
	init_thread (t, name, priority); //스레드를 초기화, 스레드의 이름, 우선순위들이 설정됨.
	tid = t->tid = allocate_tid (); // llocate_tid 함수를 호출하여 유일한 스레드 ID(tid)를 할당받음

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread; // 커널 스레드 설정: t->tf.rip에 kernel_thread 함수의 주소를 저장하여, 스레드가 실행될 때 kernel_thread 함수가 호출되도록 한다. 이 함수에 전달할 첫 번째와 두 번째 인자는 각각 t->tf.R.rdi와 t->tf.R.rsi에 저장된다. 이는 x86-64 아키텍처에서 함수 인자를 전달하는 규칙을 따름
	t->tf.R.rdi = (uint64_t) function; // kernel_thread함수의 첫번째 인자
	t->tf.R.rsi = (uint64_t) aux; // kernel_thread함수의 두번째 인자
	t->tf.ds = SEL_KDSEG; // 코드 세그먼트(cs), 데이터 세그먼트(ds, es, ss) 레지스터를 설정,  이는 스레드가 사용할 메모리 영역을 정의
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF; // eflags 레지스터에 FLAG_IF 값을 설정하여, 스레드가 실행될 때 인터럽트가 가능하도록 함.

	/* Add to run queue. */
	thread_unblock (t); // 해당 스레드를 준비 상태로 변환

	/* 현재 실행중인 스레드와 새로 생성된 스레드를 비교해서 새로 들어온 스레드의 우선순위가 높으면 CPU 양보*/
	max_priority_in_ready_preemption();
	// if (t->priority > thread_current()->priority){
	// 	thread_yield();
	// }
	
	

	return tid; // 스레드 id 반환
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	//list_push_back (&ready_list, &t->elem);
	list_insert_ordered(&ready_list,&t->elem,cmp_priority,NULL); // ready_list에 elem삽입할때 우선순위로 정렬해서 삽입하는 함수. 맨 앞에 있는 원소가 우선순위가 제일 높게하기 위해서임.
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

//ㅑ

/*추가한 코드*/
void update_min_tick_to_awake(int64_t ticks){ // 잠을 자고 있는 스레드들 중에서 최소 틱값으로 전역변수(min_tick_to_awake) 업데이트 
	// min_tick_to_awake를 깨워야 할 스레드(잠자고 있는 스레드) 중 가장 작은 tick을 갖도록 업데이트 한다. 
	min_tick_to_awake = (min_tick_to_awake > ticks) ? ticks : min_tick_to_awake;
}
// min_tick_to_awake를 static한 변수로 선언했으므로 전역변수여도 다른 소스파일에서는 사용 불가 따라서 return해주는 함수로 구현을 해주어야함. 
int64_t get_min_tick_to_awake(void){
	return min_tick_to_awake;
}


void 
thread_sleep(int64_t ticks){ // 현재 스레드를 지정된 틱(ticks)까지 잠자게 만드는 역할 (스레드를 대기상태로 만드는 함수)
	struct thread *curr = thread_current (); // !! 현재 실행중인 스레드의 정보 가져옴, 현재 실행 중인 스레드의 포인터 반환
	enum intr_level old_level;  // !! 아래 주석과 동일
	ASSERT (!intr_context ()); // !! 이 코드가 실행되는 시점에 인터럽트를 처리중이 아님을 확인
	old_level = intr_disable (); // !!현재 인터럽트를 비활성화
	if (curr != idle_thread){ // !! 현재 스레드가 idle 스레드(즉, 아무 작업도 하지 않는 스레드)가 아니라면,
		update_min_tick_to_awake(ticks); // 현재의 thread의 wakeup_ticks에 인자로 들어온 ticks를 저장 
		list_push_back (&sleep_list, &curr->elem); // 현재 스레드를 대기 상태의 스레드 리스트(sleep_list)의 끝에 추가한다.
		curr -> wakeup_tick = ticks; // 자신이 잠들고 깨어날 시각(몇시에 깰지)을 저장
	}
	thread_block();//do_schedule(THREAD_BLOCKED); // 스레드의 상태를 THREAD_BLOCKED로 변경, 스케줄러에게 스레드 전환을 요청.
	intr_set_level (old_level); // !! 아래 주석과 동일
}
	

void
thread_awake (int64_t ticks){ // 주어진 ticks 시각에 도달했을 때 잠들어 있는(sleeping)스레들을 깨우는 역할을 한다.
	min_tick_to_awake = INT64_MAX; // sleep_list에서 최소틱을 가진 스레드가 깨워지고 리스트에서 삭제가 되어도 전역변수  min_tick_to_awake 변수에는 삭제가 이루어진 스레드의 최소 틱을 가지게 되버리기 때문에 계속 else구문에 있는 update_min_tick_to_awake함수에서 최솟 값이 업데이트가 안되기 때문에  thread_awake() 를 시작할 때 최대값으로 초기화진행
	struct list_elem * e = list_begin(&sleep_list); //sleep_list의 첫번째 요소(스레드)를 가리키는 포인터(반복자,iterator)를 e에 저장
	//printf("--------------------------wakeup time: %lld---------------------------\n",ticks);
	while (e != list_end (&sleep_list)){ // sleep_list의 끝을 가리키는 반복자(iterator)를 얻음. 이 반복자는 리스트의 실제 끝 요소 다음을 가리키므로, 리스트의 끝을 확인할 때 사용됨. ->  리스트의 시작부터 끝까지 반복하면서 각 스레드의 wakeup_tick을 확인, wakeup_tick은 해당 스레드가 깨어나야 할 시간(타이머 틱)을 나타냄.
    	struct thread *t = list_entry (e, struct thread, elem); // e포인터(리스트의 현재 요소를 가리키는)를 사용하여, 해당 요소를 포함하고 있는 struct thread구조체의 시작 주소를 얻음. 즉, 리스트에 있는 elem 요소로 부터 원래의 thread 구조체의 접근 가능
    	//printf("name : %s\n" ,t->name);
		if (t->wakeup_tick <= ticks){	// 스레드가 일어날 시간이 되었는지 확인(현재 시각이 일어날 시각보다 지난 경우) ->  wakeup_tick(일어날 시간)이 주어진 ticks(현재 시간을 나타내는 타이머 틱)보다 작거나 같은지 비교, 이 조건이 참이면 스레드가 깨어날 시간임을 의미
      		e = list_remove (e);	// 해당 요소를 sleep list 에서 제거 
			//printf("<깨어야할 스레드>\n");
			//printf("ticks(t->wakeup_tick) of thread for waking up is: %lld\n", t->wakeup_tick);
			//printf("current min ticks is: %lld\n",get_min_tick_to_awake());
			//printf("now sleeplist size is : %ld\n\n", list_size(&sleep_list));
      		thread_unblock (t);	// 해당 스레드를 unblock상태로 만듦 -> Unblock상태의 스레드는 실행을 위해 준비된 상태가 되며, 스케줄러에 의해 실행가능한 상태이다.
    		max_priority_in_ready_preemption();
			}
    	else{ // 현재 검사 중인 스레드의 wakeup_tick이 아직 도달하지 않았다면, 다음 스레드로 넘어감.  list_next(e)를 통해 다음 요소로 반복자를 이동시킵니다.
      		//printf("<아직 깰 필요 없는 스레드, min_tick 업데이트>\n");
			//printf("ticks(t->wakeup_tick) of thread for waking up is: %lld\n", t->wakeup_tick);
			update_min_tick_to_awake(t->wakeup_tick);// 함수가 else 구문에 있는 이유는, 깨어나야 할 시간(tick)이 아직 되지 않은 스레드들에 대해서만 다음 깨어날 시간을 업데이트하기 위함
			//printf("current min ticks is: %lld\n",get_min_tick_to_awake());
			//printf("now sleeplist size is : %ld\n\n", list_size(&sleep_list));

			e = list_next (e); //다음 스레드로 넘어감.  list_next(e)를 통해 다음 요소로 반복자를 이동시킴.
		}
  }
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current (); //현재 실행 중인 스레드의 정보를 가져옴. thread_current() 함수는 현재 실행 중인 스레드의 포인터를 반환
	enum intr_level old_level; // intr_level은 인터럽트 레벨을 나타내는 열거형(enum) 타입. 이 변수는 현재 인터럽트 상태를 저장하는 데 사용
	//intr_context() 함수는 시스템이 현재 인터럽트 처리 중인지 아닌지 확인하는 함수 -> 인터럽트 처리중이면 True 반환 , 인터럽트 처리 중이 아니면 False 반환
	ASSERT (!intr_context ()); // 괄호 안의 조건이 참이 아닐 경우 프로그램 실행을 중단 + intr_context()함수가 false를 반환해야함. 이 코드가 실행되는 시점에 인터럽트를 처리 중이 아님을 확인

	old_level = intr_disable (); // 현재 인터럽트를 비활성화하고 이전 인터럽트 상태(레벨에는 인터럽트 활성 , 비활성 2개의 레벨이 존재)를 반환한다(이전 인터럽트 상태는 당연히 활성이니깐 당연히 활성을 반환, 이따가 다시 활성 시켜야하므로 따로 저장). -> 코드 실행 도중 인터럽트에 의해 중단되지 않도록 보장하기 위해서
	if (curr != idle_thread){ // 현재 실행중인 스레드가 idle 스레드(즉, 아무 작업도 하지 않는 스레드)가 아니라면, 
		//list_push_back (&ready_list, &curr->elem); // 현재 스레드를 준비 상태의 스레드 목록(ready list)의 끝에 추가한다. 이로써 현재 스레드는 나중에 다시 실행될 수 있게 된다.
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL); // 현재 스레드가 CPU를 양보하여 ready_list에 삽입될 때 우선순위 순서로 정렬되어 삽입 되도록 수정
	}
	do_schedule (THREAD_READY); // do_schedule 함수를 호출하여 스레드의 상태를 THREAD_READY로 변경하고 스케줄러에게 스레드 전환을 요청. 이는 실제로 다음 스레드로의 전환을 담당하는 부분
	intr_set_level (old_level); // 이전에 저장해 둔 인터럽트 레벨을 복원한다. 이로써 함수 실행 전의 인터럽트 활성 상태로 되돌아간다.
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority(int new_priority) { // 현재 실행중인 스레드의 우선순위를 변경하는 함수
	thread_current()->priority = new_priority;
	thread_current()->init_priority = new_priority;
	/* 스레드의 우선순위가 변경되었을때 우선순위에 따라 선점이 발생하도록 한다.*/
	//  만약 running_thread의 priority가 변경되었을 때, ready_list에 running_thread보다 priority가 더 큰 thread가 있으면, 더 큰 thread가 running_thread가 될 수 있도록한다. 
	//////////////////////////////project_1 Priority_Scheduling/////////////////////////////
	reset_priority();  // 현재 실행중인 스레드의 init_priority가 변하는 경우도
	max_priority_in_ready_preemption();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) { // 새로운 요소를 추가했으면 초기화는 언제나 기본
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	/* Priority donation관련 자료구조 초기화 */
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donations);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트 비활성화 되었는지 확인 -> 스케줄링 도중 인터럽트에 의한 다른 스케줄링 요청이 발생하지 않도록 -> 스케줄링 과정의 일관성 보장
	ASSERT (thread_current()->status == THREAD_RUNNING); // 현재 실행중임을 확인 -> 이는 스케줄링 함수가 실행중인 스레드에 대해서만 호출 되어야만 함.
	while (!list_empty (&destruction_req)) { // destruction_req에는 종료 되어 더이상 필요하지 않아 파괴되어야할 스레드에 대한 정보를 담고 있는 구조체 포인터가 담겨 있음. -> 해제 되어야할 스레드가 존재하는 동안 계속 반복해서 실행됨.
		struct thread *victim =  
			list_entry (list_pop_front (&destruction_req), struct thread, elem); // list_pop_front는 destruction_req리스트의 가장 맨 앞에 있는 요소를 제거하고 그 요소의 주소를 반환, destrcution_req리스트 요소의 주소가 아닌 해당 요소가 가리키는 구조체의 주소로 변환 -> Victim은 리스트 요소의 실제 스레드의 주소를 가지게 됨
		palloc_free_page(victim); // victim은 이제 해제될 스레드를 가리키게 되고 palloc_free_page를 통해서 할당된 메모리 해제
	}
	thread_current ()->status = status; // 현재 실행중인 스레드의 상태를 인자로 받은 status로 변경
	schedule (); // 실제로 스레드를 교체하는 역할을 함. -> 현재 실행중인 스레드에서 다음에 실행할 스레드로의 전환을 담당.
}

static void
schedule (void) { 
	struct thread *curr = running_thread (); // 현재 실행 중인 스레드의 포인터를 반환받아 curr 변수에 저장
	struct thread *next = next_thread_to_run (); // 다음에 실행될 스레드의 포인터를 반환받아 next 변수에 저장

	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트 비활성화 되었는지 확인 -> 스케줄링 도중에 인터럽트가 발생하면 안되니깐
	ASSERT (curr->status != THREAD_RUNNING); // 현재 스레드(curr)의 상태가 실행중이 아님을 확인 -> 현재 스레드가 '실행 중' 상태에서 빠져나와 다른 상태(예: 대기)로 전환된 후에 다음 스레드로 교체될 준비가 되어 있는지 확인하는 역할이라고 보면됨.
	ASSERT (is_thread (next)); // 다음에 실행될 스레드의 포인터(next)가 실제로 스레드인지 확인(next가 유효한 스레드 객체(인스턴스)인지 판단) ->  next가 스레드 구조체의 인스턴스인지, 그리고 그 인스턴스가 유효한 상태인지 검사
	/* Mark us as running. */
	next->status = THREAD_RUNNING; // 다음에 실행될 스레드의 상태를 실행 중(THREAD_RUNNING)으로 상태 변경

	/* Start new time slice. */
	thread_ticks = 0; //thread_ticks에는 "마지막으로 스레드가 CPU 사용을 양보(yield)한 이후부터 경과한 타이머 틱의 수"를 저장 새로운 스레드의 실행 시간을 측정하기 위해서 변수를 0으로 초기화 -> 스레드가 실행되는 동안 경과한 틱의 수를 추적.

#ifdef USERPROG // 사용자 수준 프로그램(USERPROG)을 지원하는 경우에만 활성화
	/* Activate the new address space. */
	process_activate (next); //다음에 실행될 스레드(next)의 주소 공간을 활성화 -> 프로세스의 컨텍스트를 새로운 스레드에 맞게 설정
#endif

	if (curr != next) { // 현재 실행 중인 스레드(curr)와 다음에 실행될 스레드(next)가 서로 다를 때만 내부의 코드 실행 -> 같은 스레드가 실행되는 경우 아래 작업 패스
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {// 현재 스레드가 유효하고, 종료 상태에 있고, 초기 스레드가 아닌지를 확인
			ASSERT (curr != next); 
			list_push_back (&destruction_req, &curr->elem); // 현재 스레드의 구조체를 나중에 파괴하기 위해 파괴 요청 목록에 추가
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next); //  함수는 실제로 현재 스레드에서 다음 스레드(next)로 컨텍스트 스위치를 수행 , 이 함수는 현재 실행 중인 스레드의 상태를 저장하고, 다음 스레드의 상태를 복원하여 CPU 제어권을 이전하는 작업을 포함
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
//////////////////////////////project_1 Priority_Scheduling/////////////////////////////
bool cmp_priority(const struct list_elem *new_elem, const struct list_elem *e, void *aux UNUSED){ // 스레드의 우선순위를 비교, 새롭게 ready_list에 들어갈 스레드(new elem)가 기존에 있던 스레드(e) 보다 우선순위가 큰 경우 true를 반환 -> ready_list에 psuh할때 우선순위가 제일 높은 스레드가 제일 앞에 오도록 정렬하기 위한 함수라고 생각하면 됨.
   return list_entry (new_elem, struct thread, elem)->priority > list_entry (e, struct thread, elem)->priority;
}

// 
void max_priority_in_ready_preemption(void) // 현재 수행중인 스레드와 ready_list에 있는 스레드 중 우선순위가 가장 높은 스레드를 비교하여 스케줄링
{
	if (list_empty(&ready_list) || thread_current() == idle_thread)// ready_list가 비어있지 않은지 확인
	{
		return;
	}
    
	int run_priority = thread_current()->priority; // 현재 실행중인 스레드의 우선순위 저장
	struct list_elem *e= list_begin(&ready_list); // read_list(맨 앞 원소, 우선순위 가장 높은 원소) 추출
	struct thread *t = list_entry(e, struct thread, elem); // elem원소를 가지고 실제 스레드 구조체 뽑아냄
    
	if (run_priority < t->priority) // 현재 실행 중인 스레드의 우선순위가  ready_list에 있는 가장 우선순위가 높은 스레드의 우선순위보다 작으면
	{
		thread_yield(); // ready_list에 맨 앞에있는 원소(스레드) 에게 양보
	}
}
//////////////////////////////project_1 Priority_Scheduling(donation)/////////////////////////////
bool
thread_cmp_donate_priority (const struct list_elem *l, const struct list_elem *s, void *aux UNUSED)
{
	return list_entry (l, struct thread, donation_elem)->priority > list_entry (s, struct thread, donation_elem)->priority;
}
void
donate_priority(void)
{
  int depth;
  struct thread *cur = thread_current();
	/* 현재 lock_acquire() 를 요청하는 스레드가 실행되고 있다는 자체로 
	   lock 을 가지고 있는 스레드보다 우선순위가 높다는 뜻이기 때문에 
	   if (cur->priority > lock->holder->priority) 등의 비교 조건은 필요X*/
  for (depth = 0; depth < 8; depth++){ //8로 지정한 이유는 ppt에서 하라고 함.
    if (!cur->wait_on_lock) 
		break; // wait_on_lock(현재 스레드가 기다리고 있는 락)이 없다면 기부를 할 필요가 없으므로 반복문 종료 -> wait_on_lock이 존재한다면 현재 스레드는 lock에 걸려있다는 소리이므로 그 lock을 점유하고 있는 스레드에게 자신의 priority를 넘겨주는 방식을 8번 반복
    struct thread *holder = cur->wait_on_lock->holder; // 현재 스레드가 기다리고 있는 락을 점유하고 있는 스레드
    holder->priority = cur->priority; // 양도 해줌
    cur = holder; // 현재 스레드를 holder로 업데이트, 이제 현재 스레드는 락을 소유한 스레드를 가리키게됨. 다음 반복에서 이 스레드가 기다리고 있는 락이 있는지 확인하고, 있다면 그 락의 소유자에게 우선 순위를 기부(nested donation), 즉 자기보다 앞에 있는 스레드들(nested된)에게 자신의 우선순위를 양보해준다고 보면됨
  }
}

void
remove_with_lock (struct lock *lock) //
{
  struct list_elem *e;
  struct thread *cur = thread_current(); // 현재 락을 소유하고 있는 스레드

  for (e = list_begin (&cur->donations); e != list_end (&cur->donations); e = list_next (e)){
    struct thread *t = list_entry (e, struct thread, donation_elem);
    if (t->wait_on_lock == lock) // 현재 락을 소유하고 있는 스레드가 다른 락도 소유하고 있을 수 있으므로 현재 락을 쓰기 위해 기부하고 기다리는 스레드만 donation리스트에서 제거
      list_remove(&t->donation_elem); // 락을 기다리는 스레드를 기부자 명단에서 제거
  }
}

/* donations 리스트가 비어있다면 init_priority 로 설정되고 donations 리스트에 스레드가 남아있다면 남아있는 스레드 중에서 가장 높은 priority 를 가져와야 함. */
void
reset_priority (void)
{
  struct thread *cur = thread_current (); // 현재스레드는 lock을 해제하고 기부받은 우선순위를 다시 자신의 우선순위로 바꾸려고 하는 스레드
  cur->priority = cur->init_priority; // 현재 스레드는 이제 자신의 원래 우선순위로 돌아와야함.
  if (!list_empty (&cur->donations)) { // 만약 자신에게 우선순위를 기부해준 기부자들이 아직 존재한다면(이 스레드는 락을 여러개 소유하고 있던 스레드였던 것) 남아 있는 기부자들 중에서 가장 높은 priority를 가져와야함.
    list_sort (&cur->donations, thread_cmp_donate_priority, NULL); // 남아있는 스레드들을 우선순위에 따라 정렬
    struct thread *front = list_entry (list_front (&cur->donations), struct thread, donation_elem);//렬된 donations 리스트의 첫 번째 요소를 가져옴.
    if (front->priority > cur->priority) // 기부 받은 스레드 중 가장 높은 우선 순위를 가진 스레드의 우선 순위가 현재 스레드의 초기 우선 순위보다 높은지 확인
      cur->priority = front->priority; // 만약 기부 받은 우선 순위가 현재 스레드의 초기 우선 순위보다 높다면, 현재 스레드의 우선 순위를 기부 받은 우선 순위로 업데이트
  }
}