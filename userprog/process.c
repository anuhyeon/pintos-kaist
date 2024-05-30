#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

//#include "threads/synch.h"
//#include "userprog/syscall.h"

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
struct thread* get_child(int pid); // 인자로 받은 tid/pid에 해당하는 자식 스레드를 child_list에서 찾아서 반환


/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE.  ppt에서 process_execute()함수 */
tid_t
process_create_initd (const char *file_name) { // ppt에서 process_execute()함수
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	// 우리는 프로세스(스레드)의 이름을 file_name 전체(" args-sing oneargs")가 아니라 'args-single'만을 스레드 이름으로 전달해주어야한다.
	char *thread_name, *save_ptr;
	thread_name = strtok_r(file_name," ",&save_ptr);
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (thread_name, PRI_DEFAULT, initd, fn_copy);

	//printf("thread id: %d\n", tid);// 내가 추가

	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. 
   process_fork()는 인자로 프로세스의 이름과 인터럽트 프레임 구조체를 받는다. 
   이유는 부모 프로세스가 갖고있던 레지스터 정보를 고대로 담아서 복사해야기 때문!! 
   인터럽트 프레임은 인터럽트가 호출되었을 때 이전에 레지스터에 작업하던 값들을 스택에 담는 구조체이다. 
   반환 값으로 thread_creat()을 실행하는데, 그안에 인자로 _do_fork()함수를 실행
 */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	/*-------------------------------project2 syscall fork()-------------------------------*/
	struct thread *parent = thread_current();
	memcpy(&parent->parent_if, if_, sizeof(struct intr_frame)); //  fork()함수를 호출한 시점에서 if_를 백업하지 않으면 이후에 또 다른 인터럽트가 발생하여 if_가 변할 수 있기 때문에 fork()를 호출한 시점에서의 해당 프로세스를 복제하고 싶으면 바로 백업을 해주어야함. 부모 프로세스 메모리를 복사

	tid_t pid = thread_create (name, PRI_DEFAULT, __do_fork, thread_current ()); //_do_fork()는 부모 프로세스의 내용을 자식 프로세스로 복사하는 함수 -> 자식 프로세스가 프로세스로서 초기설정을 하는 함수라고 생각
	
	if (pid == TID_ERROR) return TID_ERROR;
	//위 코드에서 thread_create 함수로 스레드가 생성되어 pid가 먼저 반환되고 부모 프로세스랑 번갈아 가면서 자식스레드는 _do_fork 함수 실행
	// 하지만 번갈아가면서 하면 안됨 -> 자식 스레드가 올바르게 초기화되고 실행 준비가 완료될 때까지 부모 스레드는 기다려야 함.
	// 이를 위해 자식 스레드는 초기화(__do_fork)가 완료된 후 세마포어를 사용하여 신호(signal)을 보냠. 
	// 부모 스레드는 sema_down()을 호출하여 이 신호를 기다림. 
	//sema_down()은 호출된 스레드(부모)를 세마포어 카운트가 0 이상이 될 때까지 블록(대기 상태)로 대기
	/*이 과정은 자식 스레드가 준비되기 전(자식 스레드가 __do_fork함수를 다 끝내기 전)에 부모 스레드가 계속 실행되어 자식에게 필요한 리소스를 불필요하게 점유하거나, 초기화되지 않은 상태의 자식 스레드를 참조하는 것 방지하는 역할*/
	struct thread *child = get_child(pid); 
	sema_down(&child->fork_sema); // sema를 자식 프로세스가 획득
	return pid;
	
	
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. 
   uint64_t *pte 이 인자는 페이지 테이블 엔트리의 주소를 가리킴 페이지 테이블 엔트리란 페이지 테이블에 있는 여러개의 페이지들 정보중 하나의 하나(엔트리)를 의미
   실제로 해당 프로세스의 페이지 개수만큼 duplicate_pte()함수를 호출해 페이지 하나 하나씩 복제한다고 보면됨. 
 */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) { // 페이지 테이블 엔트리를 하나씩을 복제하는 함수라고 보면됨. 부모의 page table을 복제하기 위해 page table을 생성한다. -> 여기서 말하는 page table은 페이징 기법에서 페이징 테이블이랑 같은 의미
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. -> 부모의 page가 커널 페이지인 경우 즉시 false 리턴 */
	if is_kernel_vaddr(va){
		return false;
	}

	/* 2. Resolve VA from the parent's page map level 4. -> 부모 스레드 내 멤버인 pml4 를 이용해 부모 페이지를 불러온다 (pml4_get_page()함수를 이용)*/
	//pml4는 4단계 페이지 매핑 체계를 사용하는 시스템의 최상위 페이지 테이블을 의미, 이는 주로 64비트 시스템에서 사용되며, 크고 복잡한 가상 주소 공간을 관리하기 위해 설계함.
	//PML4는 가상 주소를 물리 주소로 변환하는 과정에서 최상위 참조 지점으로 작용
	parent_page = pml4_get_page (parent->pml4, va); //가상 주소 va에 해당하는 페이지를 부모 프로세스의 4단계 페이지 테이블pml4에서 검색하여 해당 페이지의 정보(물리주소)를 반환한다.
	if (parent_page == NULL) {
		return false;
	}
	/* 3. TODO: Allocate new PAL_USER page for the child and set result to 새로운 PAL_USER 페이지를 할당하고 newpage에 저장
	 *    TODO: NEWPAGE.
	 	newpage는 새로 할당받은 메모리 페이지의 시작 주소입니다. 이 주소는 커널 가상 주소 공간에서 할당된 주소
	  */
	newpage = palloc_get_page(PAL_USER | PAL_ZERO); // 자식스레드에게 새로운 페이지 할당  함수가 반환하는 값은 '물리 주소'가 아니라, '커널 가상 주소'이다.
	if (newpage == NULL) {
		return false;
	}
	/* 4. TODO: Duplicate parent's page to the new page and 부모 페이지를 복사해 3에서 새로 할당받은 페이지에 넣어준다. 이때 부모 페이지가 writable인지 아닌지 확인하기 위해 is_writable() 함수를 이용
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE); // 부모 프로세스의 페이지(parent_page: 복사할 원본 데이터가 있는 메모리 페이지의 시작 주소)에서 새로 할당된 페이지(newpage)로 메모리 내용을 복사, PGSIZE: 한 페이지 전체를 복사하기 위해 지정된 페이지 크기입니다.
	writable = is_writable(pte); //
	/* 5. Add new page to child's page table at address VA with WRITABLE 페이지 생성에 실패하면 에러 핸들링이 동작하도록 false를 리턴
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. 
 * 부모 프로세스의 실행 context를 복사하는 스레드 함수이다.
 * 힌트: parent->tf(부모 프로세스 구조체 내 트랩 프레임 멤버)는 프로세스의 userland context 정보를 들고있지 않다. -> 그럼 어디에??
 * 즉, 당신은 process_fork()의 두번째 인자를 이 함수에 넘겨줘야만 한다.
 * */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0; // 이 코드는 바로 이 자식 프로세스에서 실행되는 부분으로, 자식 프로세스의 시스템 호출 반환 값으로 0을 설정하여, 자식임을 명시하는 작업 자식 프로세스가  fork() 시스템 호출의 결과로 받게 되는 반환 값을 설정합니다. UNIX 및 UNIX 계열 운영 체제에서 fork() 함수는 부모 프로세스에서는 새로 생성된 자식 프로세스의 프로세스 ID를 반환하고, 자식 프로세스에서는 0을 반환합니다. 이 규칙은 프로세스가 자신이 부모인지 자식인지를 알 수 있도록 해줌

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif
	if (parent->next_fd == 128) {
		goto error;
	}
	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.
	 * 부모로부터 자식 프로세스가 fork()를 통해 생성되면 동일한 파일 디스크립터 테이블을 갖는다. 
	 * 예를 들어 부모 프로세스가 파일 A,파일 B를 open해서 관리하고 있으면, fork()후 자식프로세스도 똑같이 File A, B를 open해서 관리하는 상태이다.
	 * 부모프로세스, 자식프로세스 두 테이블의 동일한 위치에서 File A, B를 참조할 것이다.
	 * 
	 * 자식 프로세스의 FDT는 부모의 FDT와 동일하게 해줘야 한다. 
	 * 이를 위해 부모의 FDT 페이지 배열로부터 값을 하나씩 복사해서 붙여넣기 해주어야함. 
	 * 여기서 fork()로 인해 refcnt는 1 증가할 것이다. 이 부분은 제공되는 함수 file_duplicate()를 쓰면 간단하다. 
	 * stdin, stdout은 굳이 file_duplicate()를 쓸 것 없으니(특정 파일 객체를 가리키는 게 아니라 표준 입출력 값이므로) 그냥 바로 매칭해준다.
	 * */
	//  자식 프로세스의 FDT는 부모의 FDT와 동일하게 해주는 작업
	current->fdt[0] = parent->fdt[0];
	current->fdt[1] = parent->fdt[1];
	for(int i = 3; i < 128; i++){
		if(parent->fdt[i] == NULL) continue;
		current->fdt[i] = file_duplicate(parent->fdt[i]); // 부모의 fdt 정보를 자식의 fdt에 한줄 한줄 엔트리 하나씩 복사
	}
	current->next_fd = parent->next_fd; // 부모의 현재 fd 값을 자식에게도 그대로 넘겨줌
	//이제 여기까지 자식 스레드는 이제 초기화 작업을 끝냈으니 세마를 다운시켜 부모 스레드및 다른 스레드도 작업할 수 있는 상태가 됨.
	sema_up(&current->fork_sema); 
	//if_.R.rax = 0; // 이 코드는 바로 이 자식 프로세스에서 실행되는 부분으로, 자식 프로세스의 시스템 호출 반환 값으로 0을 설정하여, 자식임을 명시하는 작업 자식 프로세스가  fork() 시스템 호출의 결과로 받게 되는 반환 값을 설정합니다. UNIX 및 UNIX 계열 운영 체제에서 fork() 함수는 부모 프로세스에서는 새로 생성된 자식 프로세스의 프로세스 ID를 반환하고, 자식 프로세스에서는 0을 반환합니다. 이 규칙은 프로세스가 자신이 부모인지 자식인지를 알 수 있도록 해줌
	process_init (); //

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_); // 유저 모드로 복귀
error:
	current->exit_status = TID_ERROR; //TID_ERROR는 일반적으로 오류가 발생했음을 나타내는 상수로, 프로세스나 스레드가 성공적으로 실행되지 않았을 때 사용. 이 값은 자식 프로세스가 추후 종료될 때 부모 프로세스에게 반환될 수 있으며, 부모 프로세스는 이 값을 통해 자식 프로세스의 생성 성공 여부를 판단할 수 있다.
	sema_up(&(current->fork_sema));
	exit(TID_ERROR);
	//thread_exit ();
}


/* Switch the current execution context to the f_name.
 * Returns -1 on fail. ppt에서 start_process()함수 
  f_name에 해당하는 명령을 실행하기 위해 현재 실행 중이던 스레드의 context를 문맥 교환하는 것이 process_exec()의 역할!
 우리가 입력해주는 명령을 받기 직전에 idle스레드든 어떤 스레드가 돌고 있었을 테니
 process_exec()에 context switching 역할도 같이 넣어줘야 하는 것. 
 */
int
process_exec (void *f_name) { // 유저가 입력한 명령어가 인자로 들어가고 , 해당 명령어를 실행하기 위해 해당 파일이름을 디스크로부터 찾아서 프로그램을 메모리에 적재하고 실행하는 함수이다.
	char *file_name = f_name; // 함수 인자로 받은 문자열은 void 형이므로 문자열 포인터로 형을 변경 (타입 캐스팅)
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if; // 인터럽트 프레임 구조체로 새로운 프로그램 실행을 위해 현재 CPU상태 정보를 인터럽트 프레임에 백업
	//_if 인터럽트 프레임 0으로 초기화
	memset(&_if, 0, sizeof _if);
	_if.ds = _if.es = _if.ss = SEL_UDSEG; //사용자 데이터 세그먼트
	_if.cs = SEL_UCSEG; // 사용자 코드 세그먼트
	_if.eflags = FLAG_IF | FLAG_MBS; // FLAG_IF는 인터럽트를 활성화하는 플래그, FLAG_MBS는 몰룸

	/* We first kill the current context */
	process_cleanup (); // 새로운 실행 파일을 현재 스레드에 담기 전에 먼저 현재 process에 담긴 context를 지워준다. (현재 프로세스에 할당된 page directory를 지운다는 이야기)

	/* And then load the binary */
	success = load (f_name, &_if); // load 함수는 주어진 file_name을 사용하여 프로그램을 메모리에 적재하고, 인터럽트 프레임 _if에 프로그램의 진입점을 설정
	//printf("-----%d------ \n",success);
	/* If load failed, quit. */
	
	if (!success){
	    palloc_free_page (f_name); // 로드 실패하면 file_name을 저장했던 페이지를 해제
		return -1;
	}
	//hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	palloc_free_page (f_name);
	/* Start switched process. */
	do_iret (&_if); // do_iret 함수는 인터럽트 리턴 명령어를 사용하여 새로운 프로그램으로의 문맥 전환(context switching)을 수행. 이는 _if에 저장된 CPU 상태로 복원

	NOT_REACHED (); // 이 매크로함수는 코드가 이 위치에 도달하지 않음을 보장. 이유는 do_iret()함수 호출이 된 이후에는 새로운 프로그램이 실행되므로 이 위치에 도달하지 않는다. 만약 도달하면 오류가 있는 것으로 간주하여 panic 발생시킴.
}

/*get_child() 함수는 pid에 해당하는 자식 스레드 구조체를 위해서 만들어둔 child_list를 순화하며 찾은 뒤 해당 자식 스레드를 반환*/
struct thread* get_child(int pid){ // 인자로 받은 tid/pid에 해당하는 자식 스레드를 child_list에서 찾아서 반환
	//printf("getchild 진입\n");
	struct thread* curr = thread_current();
	struct list* child_list = &(curr->child_list);
	for(struct list_elem* e = list_begin(child_list); e != list_end(child_list); e = list_next(e)) {
		//printf("getchild 진입22222222\n");
		struct thread* child = list_entry(e, struct thread, child_elem);
		if(child->tid == pid) {// 첫번째인자: 연결리스트의 요소를 가리키는 포인터 , 두번째 인자: e가 포함된 구조체의 타임, 세번째 인자: e의 해당 구조체 내에서의 필드 이름
			//printf("getchild 진입3333333\n");
			return child;
		}
	}
	
	return NULL;
}



/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	 /* 이 함수는 부모 프로세스가 특정 자식 프로세스의 종료를 기다리는 용도로 사용됩니다.
     * child_tid는 대기하고자 하는 자식 프로세스의 스레드 식별자입니다.
     */
	//printf("tid is %d\n",child_tid);
	// for(int i = 0; i< 1000000000;i++){

	// }
	//printf("adsfadfqwetqwuyetuqwyteuyqwteuyqwteuqywteuqywteuadfadsfsfasdfasdf\n");
	//printf(">>>>>>>>>>>>>>%d\n",child_tid);
    //printf("나는 NULL 전\n");

    struct thread *child = get_child(child_tid); // 자식 스레드 목록에서 해당 PID를 가진 자식 스레드를 찾습니다.
    if (child == NULL) // 자식 스레드를 찾지 못한 경우
	{	
		return -1; // 자식이 존재하지 않으므로 -1을 반환하여 오류를 표시합니다.
	}
    sema_down(&child->wait_sema); // 자식 스레드의 종료를 기다리기 위해 해당 세마포어를 다운합니다. // 이 호출은 자식 스레드가 sema_up을 호출할 때까지 현재 스레드(부모)를 블록합니다.
    int exit_status = child->exit_status;  // 종료 상태를 먼저 저장
	list_remove(&child->child_elem); // 자식 스레드를 부모의 자식 목록에서 제거합니다.
    sema_up(&child->free_sema); // 자식 스레드의 자원을 해제할 수 있도록 자식 스레드의 free_sema를 업합니다.
    return exit_status; // 자식 스레드의 종료 상태를 반환합니다.
}


/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

    //FDT의 모든 파일을 닫고 메모리를 반환.
    for (int i = 3; i < 128; i++){
		file_close(curr->fdt[i]);
		curr->fdt[i] = NULL;
	}
	free(curr->fdt);
	curr->fdt = NULL;
	process_cleanup ();
    //자식이 종료될 때까지 대기하고 있는 부모에게 signal을 보낸다.
    sema_up(&curr->wait_sema);
    // 부모의 signal을 기다린다. 대기가 풀리고 나서 do_schedule(THREAD_DYING)이 이어져 다른 스레드가 실행된다.
    sema_down(&curr->free_sema); //sema_down을 쓴는 이유-> 식 프로세스가 종료하기 전에 이 세마포어를 사용하여 자신을 블록 상태로 전환합니다. 이는 자식 프로세스가 종료되기 전에 부모 프로세스가 필요한 모든 자원 정리를 완료하고, 자식 프로세스의 종료 상태를 검토하며, 모든 중요한 데이터를 저장할 시간을 가질 수 있도록 하기 위함
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/*-------------------------------project2 userprog argument_stack()------------------------------------*/
// token_list에는 각 토큰이 저장되어있는 메모리 블록의 시작주소가 저장되어있음.
void argument_stack(char **token_list,int count, struct intr_frame *if_){ // 각 token들이 args임
	char* args_address[128]; // user stack에서 각 args(token)들의 시작주소를 저장하기 위해서 선언
	for(int i = count ; i >= 0 ;i--){ // 토큰 값들을 유저스택에 저장하는 코드
		if_->rsp -= (strlen(token_list[i]) + 1); // 문자열 제일 마지막에는 "\0" 이 들어가 있으므로 +1 을 해줌 strlen은 "\0"전까지의 길이만 계싼  
		memcpy(if_->rsp,token_list[i],strlen(token_list[i])+1); // 두번째 인자주소에서 첫번째 인자의 주소로 복사, 세번째 인자는 복사할 바이트 수, 즉, 토큰이 저장되어있는 원본 메모리 블록의 포인터를 가지고 스택 포인터(rsp)를 사용하여 user_stack메모리 블록으로 내용(바이트 수 필요)까지 싹다 복사
		args_address[i] = if_->rsp; // arg_addr에 user_stack에 있는 토큰들의 시작 주소를 저장
	}
	while(if_->rsp % 8 != 0 ){ // 8바이트 정렬하기 위해 패딩작업
		if_->rsp -- ; // 1 바이트씩 스택 포인터를 내려주고 생긴 1바이트 메모리에 uint8_t 타입 0 채워주기
		*(uint8_t *)(if_->rsp) = 0; // uint8_t -> 부호없는 1바이트 짜리 정수 표현(0~255범위만 표현가능)
	}
	// 각 토큰들의 시작 주소를 유저 스택에 넣기전에 가장 윗 부분에 0을 넣어주어야함.
	if_->rsp -= 8; // 주소를 8바이트 내려주고
	*(uint64_t *)(if_->rsp) = 0; //memset(if_->rsp,0,8);  // 8바이트 크기의 0을 넣어줌 //
	// 각 토큰들의 시작 주소(유저스택에서 토큰들의 시작 주소임!!!! 원본이 들어있는 메모리 블록의 시작 주소가 아님!)들을 유저스택에 저장하는 코드
	for(int i = count; i >= 0 ; i--){ 
		if_->rsp -= 8; // 주소의 크기는 8바이트기 때문에
		memcpy(if_->rsp,&args_address[i],8); // 두번째 인자에는 해당 값이 들어있는 주소를 넣어주어야한다. 따라서 우리는 해당 값이 토큰의 주소이므로 토큰의 주소가 들어있는 곳의 주소를 넣어주어야함! args_address[i]가 가리키는 주소의 값의 주소를 넣어주면됌
	}
	// argv, argc를 스택에 저장하는 경우
	//args_address[100] = if_->rsp;
	//if_->rsp -= 8;
	//memcpy(if_->rsp,&args_address[100],8);
	//if_->rsp -= sizeof(int64_t);
	//*(int64_t*)(if_->rsp) = (count+1); 

	// 가짜 리턴 주소 저장
	if_->rsp -= 8; // void 포인터도 포인터이므로 8byte크기임.
	*(uint64_t *)(if_->rsp) = 0;//void 타입은 크기가 정의되지 않기 때문에 직접 값을 할당할 수 없음. 따라서 크기가 같은 uint64_t로 함.,//memset(if_->rsp,0,8); //sizeof(void*) = 8byte//
	//token_list(argv)의 주소를 저장 *token_list = token_list[0]
	//argv[0]의 주소는 유저스탹에서 가짜 리턴 주소를 저장하고 있는 위치에서 8칸(8byte) 위에 있으므로 스택포인터에서 +8을 해줌
	if_->R.rsi = if_->rsp + 8;
	if_->R.rdi = count + 1; // argc는 맨 처음 0 값도 포함해서 +1 을 햅줌

}


/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;
	
	/*--------------------project2 userprogram f_name parsing----------------------- */
	//  문자열 "args-single onearg" 을 " "단위로 토큰화
	char* token,* save_ptr, *token_list[128]; // 128개의 문자열을 담을 수 있는 포인터 배열 선언
	int idx = 0;
	for(token = strtok_r(file_name," ",&save_ptr); token != NULL; token = strtok_r(NULL," ",&save_ptr)){
		token_list[idx++] = token; // token_list안에는 문자열의 시작주소가 담김
	}
	// for (int j = 0; j < idx; j++) {
    //     printf("%s\n", token_list[j]);
    // }
	//printf("---%d------\n",idx);
	file_name = token_list[0];
	// printf("token_list = %s \n",*token_list);
	// printf("token_list[0] = %s \n",token_list[0]);
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	/*--------------------project2 userprogram argument_stack()----------------------- */
	argument_stack(token_list,idx-1,if_);
	// printf("Size of char *: %zu bytes\n", sizeof(char *));
    // printf("Size of char **: %zu bytes\n", sizeof(char **));

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true; 
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
