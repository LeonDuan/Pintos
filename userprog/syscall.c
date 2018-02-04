#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include  "process.h"
#include <string.h>
#include "devices/shutdown.h"
#ifdef VM
#include "vm/page.h"
#endif 

#ifdef USERPROG
/* Function prototypes */
void halt(void);
int exec (const char *command);
int wait (int pid);
bool create (const char *file, unsigned int size);
bool remove (const char *file);
int open (const struct file *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
void check_addr(const void *addr);
void check_addr_buffer(const void *addr, int size, bool writing);
void check_addr_string(const char *addr);
void unpin_all_buffer(const void *addr, int size);
void unpin_all_string(const char *str);
#endif

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
	lock_init(&filesys_lock);
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


static void
syscall_handler (struct intr_frame *f UNUSED)
{
	int * sp = f->esp;
	check_addr(sp);

#ifdef VM
	thread_current()->vsp = sp;
#endif

	//get syscall number
	int call_no = *(sp);

	//go to the corresponding case
	switch(call_no)
	{
	case SYS_HALT:{//DONE
		halt();
		break;
	}
#ifdef USERPROG
	case SYS_EXIT:{//INCOMPLETE
		check_addr(sp + 1);
		int status = *(sp + 1); /*exit with reported status*/
		exit(status);
		break;
	}

	case SYS_EXEC:{//COMPLETE
		check_addr(sp + 1);
		check_addr_string(*(sp + 1));
		char *command = (char *) *(sp + 1);
		f->eax = exec(command);
		unpin_all_string(command);
		break;
	}

	case SYS_WAIT:{//INCOMPLETE
		check_addr(sp + 1);
		int pid = *(sp + 1);
		f->eax = wait(pid);
		break;
	}

	case SYS_CREATE:{//DONE
		check_addr(sp + 1);
		check_addr(sp + 2);
		unsigned int file_size = *(sp + 2);
		check_addr_string(*(sp + 1));
		char *file_name = (char *) *(sp + 1);

		lock_acquire(&filesys_lock);
		f->eax = create(file_name, file_size);
		lock_release(&filesys_lock);
		unpin_all_string(file_name);
		break;
	}

	case SYS_REMOVE:{//DONE
		check_addr(sp + 1);
		check_addr_string(*(sp + 1));
		lock_acquire(&filesys_lock);
		char *file_name = (char *) *(sp + 1);
		f->eax = remove(file_name);
		lock_release(&filesys_lock);
		unpin_all_string(file_name);
		break;
	}

	case SYS_OPEN: {//DONE
		check_addr(sp + 1);
		check_addr_string(*(sp + 1));
		lock_acquire(&filesys_lock);
		char *file_name = (char *) *(sp + 1);
		struct file* fp = filesys_open(file_name);
		lock_release(&filesys_lock);
		if(!fp){
			f->eax = -1;
		}
		else{
			f->eax = open(fp);
		}
		unpin_all_string(file_name);
		break;

	}

	case SYS_FILESIZE:{//DOME
		check_addr(sp + 1);
		lock_acquire(&filesys_lock);
		unsigned int fd = *(sp + 1);
		f->eax = filesize(fd);
		lock_release(&filesys_lock);
		break;
	}

	case SYS_READ:{//DONE
		check_addr(sp + 1);
		check_addr(sp + 2);
		check_addr(sp + 3);
		unsigned int size = *(sp + 3);
		check_addr_buffer(*(sp + 2), size, true);
		int fd = *(sp + 1);
		char* buffer = (char*) *(sp + 2);

		f->eax = read(fd, buffer, size);
		unpin_all_buffer(buffer,size);
		break;
	}

	case SYS_WRITE:{//DONE
		check_addr(sp + 1);
		check_addr(sp + 2);
		check_addr(sp + 3);
		unsigned int size = *(sp + 3);
		check_addr_buffer(*(sp + 2), size, false);
		int fd = *(sp + 1);
		char* buffer = (char*) *(sp + 2);

		f->eax = write(fd, buffer, size);
		unpin_all_buffer(buffer,size);
		break;
	}

	case SYS_SEEK:{//DONE
		check_addr(sp + 1);
		check_addr(sp + 2);
		unsigned int fd = *(sp + 1);
		unsigned int position = *(sp + 2);
		seek(fd, position);
		break;
	}
	case SYS_TELL:{//DONE
		check_addr(sp + 1);
		unsigned int fd = *(sp + 1);
		tell(fd);
		break;
	}
	case SYS_CLOSE:{//DONE
		check_addr(sp + 1);
		unsigned int fd = *(sp + 1);
		close(fd);
		break;
	}
#endif
	default:
		if(call_no >= 0 && call_no <= 20){
			printf("This system call has not been implemented!");
			exit(-1);
		}
		else{
			printf("No such system call!");
			exit(-1);
		}
	}
}




void halt(void){
	shutdown_power_off();
}

void exit (int status){
	struct list_elem* e = NULL;
	for(e = list_begin(&thread_current()->parent->children);
			e != list_end(&thread_current()->parent->children);
			e = list_next(e)) {
		struct child* c = list_entry(e, struct child, elem);
		if(thread_current()->tid == c->tid) {
			c->exit_status = status;
			break;
		}
	}
	thread_current()->exit_status = status;
	if(thread_current()->tid == thread_current()->parent->wait_for) {
		sema_up(&thread_current()->parent->wait);
	}

	thread_exit();
}

int exec (const char *command){
	check_addr(command);

	/* need a copy to get the name of the executable */
	char command_cp[1024];
	strlcpy(command_cp, command, 1024);

	char *saveptr;/*initialize a saveptr pointer as required by the strtok_r() function*/
	char *executable_name = strtok_r(command_cp, " ", &saveptr);


	lock_acquire(&filesys_lock);
	/* see if the executable name is valid */
	struct file* f = filesys_open (executable_name);


	if(f == NULL){/* if not valid */
		lock_release(&filesys_lock);
		return -1;
	}

	/* if valid */
	file_close(f);
	lock_release(&filesys_lock);
	return process_execute(command);
}

int wait (int pid){
	return process_wait(pid);
}

bool create (const char *file, unsigned int size){
	return filesys_create(file, size);
}

bool remove (const char *file){
	return filesys_remove(file);
}

int open (const struct file *fp){
	struct thread *cur_thread = thread_current();
	struct thread_file *tf = malloc(sizeof(struct thread_file));
	tf->fd = cur_thread->num_fd;
	tf->fp = fp;
	cur_thread->num_fd ++;
	list_push_back(&cur_thread->opened_files, &tf->elem);
	return tf->fd;
}

int filesize (int fd){
	struct thread_file* tf = NULL;
	/* start searching for a file with file descriptor number fd*/
	struct list_elem *e;
	for (e = list_begin(&thread_current()->opened_files); e != list_end (&thread_current()->opened_files); e = list_next (e)){
	  tf = list_entry (e, struct thread_file, elem);
	  if(tf->fd == fd){
		break;
	  }
	}
	return file_length(tf->fp);
}

int read (int fd, void *buffer, unsigned size){
	char* real_buffer = (char *) buffer;
	if(fd == 0){//read from stdin
		int i;
		for(i=0; i < size; i++){
			real_buffer[i] = input_getc();
		}
		return size;
	}
	else{//from from a file
		struct thread_file* tf = NULL;
		/* start searching for a file with file descriptor number fd*/
		struct list_elem *e;
		for (e = list_begin(&thread_current()->opened_files); e != list_end (&thread_current()->opened_files); e = list_next (e))
		{
		  tf = list_entry (e, struct thread_file, elem);
		  if(tf->fd == fd){
			break;
		  }
		}

		if(tf == NULL){
			return -1;
		}
		else{
			lock_acquire(&filesys_lock);
			int result = file_read(tf->fp, buffer, size);
			lock_release(&filesys_lock);
			return result;
		}
	}
}

int write (int fd, void *buffer, unsigned size){
	if(fd == 1){//write to stdout
		putbuf(buffer, size);
		return size;
	}
	else{//write to a file
		struct thread_file* tf = NULL;
		/* start searching for a file with file descriptor number fd*/
		struct list_elem *e;
		for (e = list_begin(&thread_current()->opened_files); e != list_end (&thread_current()->opened_files); e = list_next (e))
		{
		  tf = list_entry (e, struct thread_file, elem);
		  if(tf->fd == fd){
			break;
		  }
		}

		if(tf == NULL){
			return -1;
		}
		else{
			lock_acquire(&filesys_lock);
			int result = file_write(tf->fp, buffer, size);
			lock_release(&filesys_lock);
			return result;
		}
	}
}

void seek (int fd, unsigned position){
	struct thread_file* tf = NULL;
	/* start searching for a file with file descriptor number fd*/
	struct list_elem *e;
	for (e = list_begin(&thread_current()->opened_files); e != list_end (&thread_current()->opened_files); e = list_next (e))
	{
	  tf = list_entry (e, struct thread_file, elem);
	  if(tf->fd == fd){
		break;
	  }
	}
	lock_acquire(&filesys_lock);
	file_seek(tf->fp, position);
	lock_release(&filesys_lock);
}

unsigned tell (int fd){
	struct thread_file* tf = NULL;
	/* start searching for a file with file descriptor number fd*/
	struct list_elem *e;
	for (e = list_begin(&thread_current()->opened_files); e != list_end (&thread_current()->opened_files); e = list_next (e))
	{
	  tf = list_entry (e, struct thread_file, elem);
	  if(tf->fd == fd){
		break;
	  }
	}
	lock_acquire(&filesys_lock);
	int result = file_tell(tf->fp);
	lock_release(&filesys_lock);
	return result;
}

void close (int fd){
	struct thread_file* tf = NULL;
	/* start searching for a file with file descriptor number fd*/
	struct list_elem *e;
	for (e = list_begin(&thread_current()->opened_files); e != list_end (&thread_current()->opened_files); e = list_next (e))
	{
	  tf = list_entry (e, struct thread_file, elem);
	  if(tf->fd == fd){
		lock_acquire(&filesys_lock);
		file_close(tf->fp);
		lock_release(&filesys_lock);
		list_remove(e);
		free(tf);
		return;
	  }
	}
}

void check_addr_pin(const void *addr, bool unpin){
	if(addr == NULL){
		exit(-1);
	}
	else if(is_user_vaddr(addr) && pagedir_get_page(thread_current()->pagedir,addr)){
		return;
	}
	else{
#ifdef VM
		if(page_map_to_frame(addr,thread_current()->vsp, unpin))
			return;
#endif
		exit(-1);
	}
}

void check_addr(const void *addr) {
	check_addr_pin(addr,true);
}

void check_addr_buffer(const void* addr, int size, bool writing){
    for(int i = size - 1; i >= 0; i--){
        check_addr_pin(addr + i,false);
#ifdef VM
        struct supp_page_table_entry *spte = page_find(&thread_current()->supp_page_table,addr+i);
        if (spte && writing && !spte->writable)
            exit(-1);
#endif
    }
}

void check_addr_string(const char* str){
    check_addr_pin(str,false);
    while(*str != 0){
        check_addr(++str);
    }
}

void unpin_all_buffer(const void *addr, int size) {
#ifdef VM
    for(int i = size - 1; i >= 0; i--){
        page_unpin(&thread_current()->supp_page_table,addr+1);
    }
#endif
}

void unpin_all_string(const char *str) {
#ifdef VM
    page_unpin(&thread_current()->supp_page_table,str);
    while(*str != 0){
        page_unpin(&thread_current()->supp_page_table,++str);
    }
#endif
}
