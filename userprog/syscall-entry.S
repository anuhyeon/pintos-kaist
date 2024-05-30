#include "threads/loader.h"

.text
.globl syscall_entry
.type syscall_entry, @function
syscall_entry:

 	/* Callee-saved registers의 임시 저장소 */
	movq %rbx, temp1(%rip)		// RBX 레지스터 값을 전역 변수 temp1에 저장
	movq %r12, temp2(%rip)     /* callee saved registers R12 레지스터 값을 전역 변수 temp2에 저장*/
	
	/* 유저 스택 포인터를 RBX에 저장. 유저 스택의 현재 위치를 백업. */
	movq %rsp, %rbx            /* Store userland rsp 현재 유저 모드의 스택 포인터(RSP)를 RBX에 백업  */
	
	/* TSS에서 커널 스택 포인터 읽기. 이동은 커널 스택으로의 전환을 의미. */
	//TSS의 주소를 R12에 로드
	//TSS 내 커널 스택 포인터를 R12로 로드
	//R12에서 커널 모드의 스택 포인터를 RSP에 로드
	movabs $tss, %r12         /*syscall이 발생하여 커널모드로 전환을 하려면 스택 포인터가 유저 스택에서 커널 스택으로 가리켜야한다. 우리는 커널 모드에서 커널 스택에 push/pop을 진행해야함. 커널 스택을 가리키고 있는 포인터를 알아야함. 커널 스택을 가리키는 주소는 tss에 저장되어있음. */
	movq (%r12), %r12          /*위에서 tss에 저장된 커널 스택 주소를 r12에 넣어줌. 이를 통해 우리는 이제 커널 스택 포인터를 사용할 수 있게됨 -> 커널 모드로 변함 */
	movq 4(%r12), %rsp         /* Read ring0 rsp from the tss */
	
	/* Now we are in the kernel stack  커널 스택으로 전환 완료, 이제 커널 스택에 유저 스택에서의 상태를 백업. */
	push $(SEL_UDSEG)      /* if->ss. 스택 세그먼트 레지스터 백업 -> 다 커널 스택에 백업하는 것 */
    push %rbx              /* if->rsp. 백업된 유저 스택 포인터 */
    push %r11              /* if->eflags. 현재 프로세서 상태 플래그 */
    push $(SEL_UCSEG)      /* if->cs. 코드 세그먼트 레지스터 백업 */
    push %rcx              /* if->rip. 다음 실행할 명령의 주소 (인터럽트 발생 시점) */
	
	/* 예외 코드와 인터럽트 번호 공간을 확보 (실제 사용 전에 오버라이드 될 수 있음) */
	subq $16, %rsp         /* skip error_code, vec_no 16바이트 공간 확보 */
	
	/* 유저 데이터 세그먼트와 스택 세그먼트 레지스터 저장 */
	push $(SEL_UDSEG)      /* if->ds */
	push $(SEL_UDSEG)      /* if->es */
	
	/* 레지스터들을 커널 스택에 백업 */
	push %rax              /* 누적기 레지스터 */
    movq temp1(%rip), %rbx
    push %rbx              /* 베이스 레지스터 */
    pushq $0               /* 의미없는 0 값 (자리 보존용) */
    push %rdx              /* 데이터 레지스터 */
    push %rbp              /* 베이스 포인터 레지스터 */
    push %rdi              /* 첫 번째 함수 호출 인자 */
    push %rsi              /* 두 번째 함수 호출 인자 */
    push %r8               /* 세 번째 함수 호출 인자 */
    push %r9               /* 네 번째 함수 호출 인자 */
    push %r10              /* 임시 레지스터 */
    pushq $0               /* R11 스킵 */
	
	/* 추가 레지스터 백업 */
	movq temp2(%rip), %r12
	push %r12              /* 12번째 레지스터 */
    push %r13              /* 13번째 레지스터 */
    push %r14              /* 14번째 레지스터 */
    push %r15              /* 15번째 레지스터 */
	
	/* 커널 스택 포인터를 함수 인자로 넘기며 syscall_handler 호출 -> 스택 포인터를 함수 호출에 사용하기 위해 RDI에 저장 */
	movq %rsp, %rdi           // RDI 레지스터는 일반적으로 함수 호출 시 첫 번째 인자로 사용됨. 즉, 이 명령은 현재 커널 스택의 위치를 가리키는 포인터를 syscall_handler 함수의 첫 번째 인자로 전달

check_intr:
    btsq $9, %r11                  // 인터럽트 복구 여부 확인
    jnb no_sti                     // 인터럽트 복구가 필요 없으면 no_sti로 점프
    sti                            // 인터럽트 복구              /* restore interrupt */
no_sti:
	movabs $syscall_handler, %r12            /* 우리가 구현한 syscall_handler()함수를 호출하면서 아래 어셈블리어를 수행, 맨 밑에sysretq 라는 어셈블리어는 유저모드로 복귀하는 어셈블리어 이므로우리는 syscall_handler()함수안에 C언어로 do_iret()함수를 넣어줄 필요가 없다. */
	call *%r12    /* r12에 저장된 syscall_hander()함수를 실행 */
	/* syscall_handler 실행 완료 후, 백업했던 레지스터들을 복원, 커널 스택에서는 pop으로 사라지게 됨 */
	popq %r15      /* syscall_hander()함수가 다 끝나면, 아래 어셈블리어로 커널 스택에 저장된 인터럽트 프레임 레지스터 값들을 pop해준다. */
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $32, %rsp                 // 스택 정리
    popq %rcx                      // 다음 명령 주소 복원
    addq $8, %rsp                  // 스택 조정
    popq %r11                      // EFLAGS 복원
    popq %rsp                      // 스택 포인터 복원

    sysretq                        // 유저 모드로 복귀

.section .data
.globl temp1
temp1:
.quad	0
.globl temp2
temp2:
.quad	0
