#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
/*-------------------project2 argment passing-----------------------*/
void argument_stack(char **token_list, int count, struct intr_frame *if_);

int process_add_file_in_FDT(struct file *f);
struct file *process_get_file_from_fdt(int fd);
void process_close_file(int fd);
struct thread *get_child(int pid);
#endif /* userprog/process.h */
