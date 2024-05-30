#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release (inode_sector, 1);
	dir_close (dir);

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails.
 * filesys_open()함수는 주어진 파일 이름에 대해 파일 시스템에서 해당 파일을 찾고 해당 파일을 조작할 수 있는 sturct file객체를 반환하는 역할을 수행하는 함수
 *  */
struct file *
filesys_open (const char *name) { // 파일 시스템에서 주어진 이름(name)의 파일을 열기 위한 함수이다. filesys_open함수는 파일 이름을 받아 해당 파일에 대한 파일 객체(struct file)을 반환한다. 파일 객체는 파일에 대한 읽기, 쓰기, 닫기 등의 작업을 수행할 때 사용
	struct dir *dir = dir_open_root (); // 루트 디렉토리(파일 시스템이 최상위 디렉토리)를 열어 struct dir 객체를 반환
	struct inode *inode = NULL; // inode는 NULL로 초기화

	if (dir != NULL) 
		dir_lookup (dir, name, &inode); // dirlookup함수는 dir 디렉토리에서 name이라는 이름을 가진 파일을 찾아서 그 파일의 inode 정보를 inode 포인터 변수에 저장 -> dir_lookup 함수는 파일이 존재하면 해당 파일의 inode 구조체를 inode 변수에 연결하고, 파일이 존재하지 않으면 inode는 NULL로 유지
	dir_close (dir); // 파일 탐색이 끝나면 더 이상 디렉토라 객체가 필요하지 않으므로 dir_close함수를 호출하여 디렉토리를 닫음. -> 열린 디렉토리 자원을 정리하는 과정

	return file_open (inode); // file_open 함수는 inode를 인자로 받아 해당 inode를 갖는 파일 객체(struct file)을 생성하고 반환
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
