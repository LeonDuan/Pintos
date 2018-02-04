#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

void syscall_init (void);

#ifdef USERPROG
void exit (int status);
#endif

//this struct is used for storing info about files opened in a thread
struct thread_file{
	struct file* fp;
	int fd;
	struct list_elem elem;
};

struct lock filesys_lock;

#endif /* userprog/syscall.h */
