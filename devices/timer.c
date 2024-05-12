#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) { // 운영체제가 부팅괸 이우호 경과한 타이머 틱수를 반환현재 ticks 값을 반환
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks; // ticks는 전역 변수 -> 핀토스 내부에서 시간을 나타내기 위한 값으로 부팅 이후에 10ms마다 1씩 증가
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) { // then(특정 시간)이후로 경과된 시간을 반환
	return timer_ticks () - then;
}


/* Suspends execution for approximately TICKS timer ticks. */ //1tick이 경과 할때 마다 타이머 인터럽트 발생
void 
timer_sleep (int64_t ticks) { // 스레드를 특정시간동안 대기 상태(block state)로 만들어주는 함수 , ticks라는 인자를 받음. 이 값은 스레드가 얼마나 오랫동안 대기할지를 나타내는 시간(타이머 틱 단위)을 의미
	int64_t start = timer_ticks (); // start에 시스템의 현재 시간(ticks)을 나타냄 timer_sleep 함수가 호출되었을 때의 현재 시간을 나타냄

	ASSERT (intr_get_level () == INTR_ON); // 인터럽트가 활성화 되어있지 않으면 assert 경고 INTR_ON은 인터럽트가 활성화 되어있음을 나타냄.
	//while (timer_elapsed (start) < ticks) // busy waiting 발생, timer_elapsed(start)의 반환값이 ticks보다 작은 동안 계속 실행됨. timer_elapsed(start)는 start로부터 지난 시간을 반환. 즉, 이 루프는 스레드가 지정된 시간만큼 대기하도록 합
	//	thread_yield (); // thread_yield() 함수는 현재 스레드가 CPU를 포기하고 준비 상태의 다른 스레드에게 실행을 넘기도록 함. 이는 현재 스레드를 준비 큐의 끝에 다시 넣음.

	if(timer_elapsed(start) < ticks){
		thread_sleep(start + ticks);
	}

}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. 1tick이 경과 할 때마다 타이머 인터럽트 발생, 이 타이머 인터럽트가 발생할 때 awake 작업을 포함 시키면 ticks가 증가 할때 마다 깨워야할 스레드가 있는지 찾고, 깨워주는 작업을 진행할 수 있음.  */ 
static void
timer_interrupt (struct intr_frame *args UNUSED) {  // imer_interrupt 함수는 운영 체제의 타이머 인터럽트 핸들러로 사용됨. 이 함수는 시스템 타이머가 정해진 시간마다 인터럽트를 발생시킬 때 호출되어, 시스템의 시간 관리와 스레드 스케줄링을 관리
	ticks++; //타이머 인터럽트가 발생 할 때 마다 1tick 증가 -> 시스템이 시작된 이후로 경과한 총 타이머 틱의 수를 추적하는 역할.
	thread_tick (); // 이 함수는 현재 실행 중인 스레드에 대한 타이머 틱 처리를 수행. 예를 들어, 스레드의 시간 할당량(time quantum)을 감소시키거나, 스레드의 상태를 관리하는 데 사용될 수 있음.
	thread_awake(ticks); // ticks 값을 기반으로 대기 중인 스레드 중에서 깨워야 할 스레드가 있는지를 확인하고, 깨워야 할 시간이 도달한 스레드를 준비 상태로 변경
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
