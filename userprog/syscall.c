#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/synch.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

#include "devices/input.h"
#include "lib/kernel/stdio.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *f);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void check_address(void* addr);
void halt(void);
void exit(int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);


int process_add_file_in_FDT(struct file *file);
int open (const char *file);

struct file *process_get_file_from_fdt(int fd);
int filesize(int fd);

void seek(int fd, unsigned position);
unsigned tell (int fd);

int fork(const char *thread_name, struct intr_frame *f); // 선언

int exec(const char *cmd_line);

int wait (tid_t pid);

struct thread* get_child(int pid); // 인자로 받은 tid/pid에 해당하는 자식 스레드를 child_list에서 찾아서 반환



void
syscall_init (void) { // syscall함수내 에서 syscall이라는 어셈블리어가 실행되면 syscall_init()함수가 실행됨 이 함수 안에 syscall_entry()함수로 이어지고 syscall_entry는 syscall_entry.S를 실행
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	
	/*-------------------------project2 userprogram filesys read()--------------------------------*/
	lock_init(&filesys_lock); // lock을 선언하고 초기화 -> 스레드가 파일에 접근하는 동안 다른 스레드가 접근하면 안됨.
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	/* 유저 스택에 해당 시스템 콜 넘버가 저장되어 있다, 시스템 콜 넘버를 가져옴. */
	int sys_number = f->R.rax; // rax: 시스템 콜 넘버
    /* 
	인자 들어오는 순서:
	1번째 인자: %rdi
	2번째 인자: %rsi
	3번째 인자: %rdx
	4번째 인자: %r10
	5번째 인자: %r8
	6번째 인자: %r9 
	syscall_handler를 호출할 때 이미 인터럽트 프레임에 해당 시스템 콜 넘버에 맞는 인자 수 만큼 들어있다.
	따라서 각 함수 별로 필요한 인자 수 만큼 인자를 넣어줌. 이때 rdi,rsi 이친구들은 특정 값이 이쓴게 아니라 그냥 인자를 담는 그릇의 번호 순서다.
	*/
	//printf("------------sys_number is %d-----------\n", sys_number);
	// TODO: Your implementation goes here.
	switch(sys_number) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = f->R.rax = fork(f->R.rdi, f);		
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
		case SYS_CREATE:
			create(f->R.rdi, f->R.rsi);
			break;	
		case SYS_REMOVE:
			remove(f->R.rdi);
			break;		
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;		
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;	
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);		
		case SYS_TELL:
			tell(f->R.rdi);		
		case SYS_CLOSE:
			close(f->R.rdi);
		default:
			thread_exit();
	}
	//printf ("system call!\n");
	//thread_exit (); //-> exit()랑 같음.
	//do_iret(f);  -> syscall 어셈블리어에 다 구현되어있음 따라서 굳이 do_iret()함수 호출할 필요 없음.
}

/* pintos 종료시키는 함수 */
void halt(void){
	power_off();
}
/* 현재 프로세스를 종료시키는 시스템 콜 -> halt()처럼 시스템 전체를 종료시키는게 아닌 현재 돌고 있는 프로세스만 종료 시킴. */
/* 정상적으로 종료 --> status는 0 status는 프로그램이 정상적으로 종료됐는지 확인 하는 용도임*/
void exit(int status)
{
	struct thread *t = thread_current();
	printf("%s: exit(%d)\n", t->name, status); // Process Termination Message
	thread_exit(); // thread_exit()함수 내에 process_exit()함수가 구현되어있는데 이것 또한 우리가 구현해야함.
}

/* 파일 생성하는 시스템 콜  성공이면 true, 실패면 false를 반환 */
bool create (const char *file, unsigned initial_size) {
	check_address(file); 
	bool succ;
	// 동기화 추가 할것
	lock_acquire(&filesys_lock);
	succ = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	return succ;
	
}
/*파일을 제거하는 함수로, 이때 파일을 제거하더라도 이전에 파일을 오픈했었더라면 해당 오픈 파일은 close 되지 않고 그대로 켜진 상태로 남아있는다.*/
bool remove (const char *file) {
	check_address(file);
	bool succ;
	// 동기화 추가 할것
	lock_acquire(&filesys_lock);
	succ = filesys_remove(file);
	lock_release(&filesys_lock);
	return succ;
}
/*read() 함수에서는 fd = 1 인경우(시스템콜 넘버가 1인 경우, 콘솔에 출력해줘야하는 경우) 버퍼에 있는 값에서 size만큼 출력하도록 함수를 구현했다.
  이미 구현된 함수인 file_write()를 이용해 size byte만큼 버퍼로부터 값을 읽어서 file에 작성
  putbuf()함수는 주로 시스템 수준의 프로그래밍에서 사용되는 함수로, 지정된 버퍼의 내용을 주어진 크기만큼 직접 출력 장치에 쓰기위해 사용.
  이 함수는 buffer에 저장된 데이터를 바로 화면이나 다른 출력 스트림에 출력하는 데 쓰이며, size 매개변수는 출력할 데이터의 바이트 수를 지정
  putbuf() 함수는 buffer에서 시작하는 size 바이트의 데이터를 출력 장치로 직접 전송합니다. 이 함수는 일반적으로 운영 체제의 커널 모드에서 실행되며, 사용자 공간 애플리케이션에서는 직접 접근이 제한됨.
*/
int write (int fd, const void *buffer, unsigned size) { // 1번 파일 디스크립터 STDOUT을 갖는 파일을 생성해 아 친구를 write()로 출력해주는 작업을 해주어야한다.
	check_address(buffer);
	struct file *file_obj = process_get_file_from_fdt(fd);
	int len;
	if(fd == 0 || fd == 2){
		return -1;
	}
	else if (fd == 1){ // 파일1 대신 STDOUT_FILENO(stdio.h에 매크로로 선언되어있음)을 써도 됨.
		putbuf(buffer, size); // putbuf는 버퍼에 들어있는 값을 size만큼 출력하는 함수이다. 파일 디스크립터 번호가 1인경우(출력 하라는 경우)에 한해 값을 출력하는 코드 
		len = size;
	}
	else{
		lock_acquire(&filesys_lock);
		len = file_write(file_obj,buffer,size);
		lock_release(&filesys_lock);
	}
	return len;
}

/*파일을 사용하거나 파일을 실행시키기 위해서는 항상 먼저 해당 파일을 open해야함.*/
int open (const char *file) { // 해당 파일을 가리키는 포인터를 인자로 받음. 파일을 open해야 fd가 생성됨
	check_address(file); // 먼저 주소 유효한지 늘 체크

	lock_acquire(&filesys_lock);
	struct file *file_obj = filesys_open(file); // filesys_open()함수는 주어진 이름을 갖는 파일을 open하는 함수이다. 파일 구조체안에는 inode라고 실제 데이터에 블록에 접근할 수 있는 정보가 저장되어있음(start) -> 이 과정에서 inode가 나오는데 inode는 우리가 입력한 파일이름을 컴퓨터가 알고 있는 파일 이름으로 바꾸어주는 과정이라고 생각하면됨. 이 filesys_open()은 file_open()을 반환하고, 이 file_open은 해당 파일 구조체에 inode 관련 정보를 멤버로 넣어준 뒤 다시 file 구조체를 반환한다.
    lock_release(&filesys_lock);

	// filesys_open()함수를 사용해서 열려고 하는 파일 객체 정보를 file_obj에 저장.
	
	// 파일이 제대로 생성됐는지 확인
	if (file_obj == NULL) {
		return -1;
	}


	int fd = process_add_file_in_FDT(file_obj); // 만들어진 파일을 스레드 내 fdt 테이블에 추가

	// 만약 파일을 열 수 없으면 -1을 받음
	if (fd == -1) {  // fd가 -1이면 보통 파일 디스크립터를 사용하여 파일을 열거나, 소켓 연결 등을 수행할 때 해당 작업이 실패했음을 나타냄.
		file_close(file_obj);
	}

	return fd;
}

 /* 파일을 현재 프로세스의 fdt에 추가 */
int process_add_file_in_FDT(struct file *file) {
	struct thread *t = thread_current();
	//struct file **file_descriptor_table = t->fdt;
	int fd = t->next_fd; //fd값은 2부터 출발
	while (t->fdt[fd] != NULL && fd < 128) {
		fd++;
	}

	if (fd >= 128) {
		return -1;
	}
	t->next_fd = fd;
	t->fdt[fd] = file;
	return fd;
}

/*해당 파일로부터 값을 읽어 버퍼에 넣는 함수 -> read()
  read() 함수는 인자로 파일디스크립터, 버퍼, 그리고 버퍼의 사이즈를 인자로 받는다.
  먼저 버퍼가 유효한지 check(check_address()), 그 다음 process_get_file로 파일 객체를 찾는다.
  깃북에서 fd = 0 인경우(STDIN= 표준 입력) input_get() 함수를 사용해서 키보드 입력을 읽어오도록 하라고 함.
  그 다음 파일을 읽어들일 수 없는 경우에는 -1을 반환을 하라고함.
  예를 들어 fd = 1인 경우는 STDOUT(표준 출력)을 나타내므로 -1을 반환하면 됨.
  그 외의 나머지는 fd로 부터 파일 객체를 찾은뒤 size 바이트 크기 만큼 파일을 읽어 버퍼에 넣어준다.
  이때 lock을 사용해서 커널이 파일을 읽는 동안 다른 스레드가 이 파일을 건드리는 것을 막아줌. 그렇지 않으면 원래 읽어 들이려고 하는 파일값과 다른 값을 읽는 경우가 생길 수 있음.
  이를 위해서 먼저 userprog/syscall.h에 파일 시스템과 관련된 lock인 filesys_lock을 하나 선언한다. 
  또한 시스템 콜을 초기화하는 syscall_init()에도 역시 lock을 초기화 하는 함수 lock_init()을 선언해주어야함.
*/
int read(int fd, void *buffer, unsigned size) {
	// 유효한 주소 공간 인지 체크 -> read는 버퍼의 시작과 끝을 확인해 주어서 유효한 공간인지 check -> read는 버퍼에 넣는 작업이니깐
	check_address(buffer); // 버퍼 시작 주소 체크
	check_address(buffer + size -1); // 버퍼 끝 주소도 유저 영역 내에 있는지 체크 버퍼에 파일을 넣을 사이즈 만큼의 공간이 있는지 check하는 작업
	unsigned char *buf = buffer;
	int len;
	struct file *file_obj = process_get_file_from_fdt(fd);
	if (file_obj == NULL) { // 예외 처리
		return -1;
	}
	// fd 값이 STDIN일 때
	if (fd == 0) { 
		uint8_t ch; // 1바이트 짜리 부호 없는 정수 받기
		for (int len = 0; len < size; len++) { // 사용자 입력을 버퍼에 저장
			ch  = input_getc(); // 사용자로부처 한 문자를 입력받는 함수로 키보드로 입력을 받음. -> 한번에 하나의 문자만 입력 받을 수 있음. 키보드에서 발생하는 인터럽트에 의해 호출되는 입력 처리 함수임.
			*buf = ch;
			buf ++ ;     // 위 두줄 합쳐서 *buf++ = ch; 이렇게 해줘도 됨.
			// if (ch == '\n') { // 엔터값
			// 	break;
			// }
		}
	}
	// fd 값이 STDOUT일 때 혹은 error(=2)일때
	else if (fd == 1 || fd == 2){
		return -1;
	}
	else { // fd가 표준입출력이 아닌 경우
		lock_acquire(&filesys_lock);
		len = file_read(file_obj, buffer, size); //file_read()함수는 주어진 파일 객체에서 데이터를 읽어서 주어진 버퍼에 저장하는 역할을 함. 이제 wirte()로 버퍼를 출력하면됨. file_read()함수가 반환하는 값은 실제로 파일로부터 읽은 데이터의 바이트 수이다. 이는 요청된  size 파라미터 값과 다를 수 있따. 청한 size 만큼의 데이터를 읽기 전에 파일의 끝(EOF)에 도달하면, 실제로 읽을 수 있는 데이터는 더 적을 수 있음. 보통은 인자로 받은 size랑 반환 값이랑 같음. 파일 읽어들일 동안만 lock 걸어준다.
		lock_release(&filesys_lock);
	}
	return len;
}
		


/*열려있는 파일을 디스크립터에서 찾아 해당 파일의 크기를 반환하는 함수 -> 파일크기는 어디에 저장?
	struct file -> struct inode -> struct inode_disk data -> off_t length
위에서 inode는 해당 파일을 컴퓨터가 읽을 수 있는 형태로 파일의 메타데이터를 담는 곳이라고 보면됨. 즉, 파일의 메타데이터를 담고 있는 자료구조가 inode이다.
inode_disk 구조체는 inode 데이터를 디스크로부터 읽어들이는 역할을 함 -> 해당 구조체 안 멤버에 메모리 블록에 대한 위치정보와 파일 크기(길이)정보가 담겨있음.
filesys/file.c에 file_length()라는 함수가 있고, 이 함수는 파일 구조체 포이너를 인자로 받아 파일의 메아데이터 inode안에 있는 length를 반환
*/
int filesize(int fd){ // 열려있는 파일을 디스크립터에서 찾아 해당 파일의 크기를 반환하는 함수
	struct file *file_obj = process_get_file_from_fdt(fd);
	if (file_obj == NULL) {
		return -1;
	}
	return file_length(file_obj);
}
/*fd값을 넣으면 파일디스크립터 테이블애서 해당 파일을 가리키는 구조체를 찾아 반환하는 함수*/
struct file *process_get_file_from_fdt(int fd){
	if (fd < 0 || fd >= 128) { // fd가 fdt에 없으면 null
		return NULL;
	}
	/* 파일 디스크립터에 해당하는 파일 객체를 리턴*/
	struct thread *t =  thread_current();
	//struct file **file_descriptor_table = t->fdt;
	return t->fdt[fd];
}

/*seek()함수의 역할은 주어진 파일 디스크립터에 해당하는 파일에서 읽기/쓰기 포인터를 지정된 위치로 이동시키는 역할을 함.
  이 함수를 통해 파일 내에서 데이터를 읽거나 쓸 위치를 조정할 수 있음.
  1. 먼저 fd가 0,1(표준 입출력)인지 확인 -> 위치 이동을 지원X (준 입력과 출력은 데이터 스트림의 특성상 위치를 임의로 변경할 수 없기 때문)
  2. fd에 해당하는 파일을 fdt를 통해 가져옴 process_get_file_from_fdt()활용 -> 이 함수에서 반환하는 파일 구조체 포인터는 포인터는 파일의 메타데이터와 상태 정보를 포함하는 구조체를 가리킴.
  3. 주소 유효성 검사
  4. 파일 포인터 이동 -> file_seek()함수를 호출하여 파일의 읽기,쓰기 포인터를 사용자가 지정한 position으로 이동시킴.
  position은 파일의 시작점에서부터의 오르셋을 바이트 단위로 나타냄. -> 위치 설정을 통해 이후 파일 작업(읽기,쓰기)을 지정된 위치에서 시작할 수 있게됨.
*/
void seek(int fd, unsigned position){ 
	if(fd == 0 || fd == 1) return;
	struct file* file_obj = process_get_file_from_fdt(fd); 
	if(file_obj == NULL) return;
	check_address(file_obj);
	file_seek(file_obj,position);
}
/*파일을 읽으려면 어디서부터 읽어야하는지에 대한 위치position을 파일내 구조체 멤버에 정보로 저장. fd값을 인자로 넣어주면 해당 파일의 position을 반환하는 함수이다.*/
unsigned tell (int fd) {
	if(fd == 0 || fd == 1) return;
	struct file *file_obj = process_get_file_from_fdt(fd);
	check_address(file_obj);
	if (file_obj == NULL) return;
	return file_tell(fd); // file_tell()함수는 해당 file의 position을 반환
}
/*close()함수는 열려있는 파일을 파일 디스크립터 테이블에서 찾아 해당 파일을 닫는 함수
  file_close()를 활용, file_close()는 file구조체 포인터를 인자로 받아 해당 파일을 종료 시키는 역할을 함.
  이를 위해 현재 스레드 내 파일 디스크립터 테이블에 들어 있는 해당 파일을 가리키는 포인터를 테이블 내에서 제거해준다.
*/
void close(int fd){
	if(fd == 0 || fd == 1) return;
	struct file* file_obj = process_get_file_from_fdt(fd); 
	if(file_obj == NULL) return;
	check_address(file_obj);
	file_close(file_obj);
}


/*자식 스레드를 생성하기 전에 parent_if에 대해서 memcpy()를 이용해 인자로 받은 if_(유저 스택을 가리키는 rsp가 저장되어있음)을 parent 스레드 내에 우리가 설정한 구조체 멤버인 parent_if에 붙여넣기 해준다. -> parent_if에는 유저 스택 정보가 담기게 된다.
  이어서 thread_creat()를 진행해 자식 스레드를 생성하고, 이후 위의 get_child()함수를 통해 해당 sema_fork 값이 1이 될 때까지(= 자식 스레드 load가 완료될 때까지)를 기다렸다가 끝나면 pid 반환.
*/
int fork(const char *thread_name, struct intr_frame *f)
{
    return process_fork(thread_name, f);
}
/*현재 프로세스를 주어진 인자와 함께 커맨드라인으로 주어진 이름을 갖는 실행 파일로 바꾸어야함.
  exec()함수는 성공시 어떠한 값도 리턴하지 않고, 실패시에는 프로세스는 exit(-1)과 함께 종료 -> 어떠한 이유로 인해 프로그램이 load 혹은 run을 못했을 경우.
  이 함수는 exec 스레드 이름을 바꾸지 않는다. 파일 디스크립터는 exec call에도 open을 유지해야 한다는 것을 명심 -> 프로세스의 실행 코드는 새로운 프로그램으로 변경되지만 스레드 자체의 식별자나 이름은 그대로 유지됨.
  exec() 함수 호출 시, 이미 열려 있는 파일 디스크립터들은 닫히지 않고 유지, 이는 exec() 호출 이후에도 동일한 파일 디스크립터를 통해 파일이나 다른 입출력 리소스에 접근할 수 있음을 의미
  위 내용은 깃북 내용
  exec()함수는 현재 프로세스를 명령어로 입력받은 실행 파일로 변경하는 함수임. 하지만 실행중인 스레드의 이름
*/
int exec(const char *cmd_line){ // exec() 함수를 사용할 때, 해당 프로세스는 지정된 새 실행 파일로 완전히 대체되며, 원래의 프로세스는 다시 돌아오지 않음
    check_address(cmd_line); // 주소 유효성 검사: file_name이 유효한 사용자 주소인지 확인
    if (process_exec(cmd_line) == -1) { // process_exec를 호출하여 새 프로그램 실행
        return -1; // 실행 실패 시 -1 반환
    }
    NOT_REACHED(); // process_exec가 성공적으로 실행되면 이 코드는 도달하지 않음
    return 0; // 정상적으로 실행되지 않는 경우를 위한 리턴(이 코드는 실행되지 않음)
}
int wait (tid_t pid){
	return process_wait(pid);
}

// int exec2(const char *cmd_line){ 
//     check_address(cmd_line); // 주소 유효성 검사: file_name이 유효한 사용자 주소인지 확인

//     int size = strlen(cmd_line) + 1; // 파일 이름의 길이 계산 (NULL 종료 문자 포함) 
//     char *fn_copy = palloc_get_page(PAL_ZERO); // 새 페이지를 할당하고 0으로 초기화 -> process_initd참고
//     if ((fn_copy) == NULL) { // 페이지 할당 실패시
//         exit(-1); // 실패한 경우 프로세스 종료
//     }
//     strlcpy(fn_copy, cmd_line, size); // 할당된 페이지에 파일 이름 복사

//     if (process_exec(fn_copy) == -1) { // process_exec를 호출하여 새 프로그램 실행
//         return -1; // 실행 실패 시 -1 반환
//     }

//     NOT_REACHED(); // process_exec가 성공적으로 실행되면 이 코드는 도달하지 않음
//     return 0; // 정상적으로 실행되지 않는 경우를 위한 리턴(이 코드는 실행되지 않음)
// }

void check_address(void* addr)
{  
	struct thread *cur_t = thread_current();
	/*포인터가 가리키는 주소가 유저영역의 주소인지 확인*/
	/*1. 포인터기 널 포인터인 경우
	  2. 포인터가 가리키는 주소가 매핑되지 않은 주소인 경우
	  3. 주어진 주소 addr이 사용자 가상 주소 영역에 속하는지 ex) write(int fd,void *buf, ... ) 에서 buf의 주소 유효성 검사를 해주어야함. 이 포인터는 사용자 프로그램이 참조하고자 하는 메모리 위치를 가리킨다. 시스템 콜에서 인자로 전달된 포인터는 보통 유저 스택, 힙, 또는 데이터 영역의 메모리 주소를 가리킴*/ 
	/*잘못된 접근일 경우 프로세스 종료*/
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(cur_t -> pml4, addr)==NULL){
		exit(-1);  // exit()함수에 -1을 인자로 넣으면 비정상 종료
	}
}