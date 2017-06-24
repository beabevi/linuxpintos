#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/console.h"
#include "devices/input.h"
#include "process.h"
#include "pagedir.h"

//static struct lock syscall_lock; 

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	//lock_init(&syscall_lock); 
}

// Performs the exit of the current thread if a syscall is done with invalid parameters 
static void bad_parameters_exit(void){
	thread_current()->parent->exit_status = -1; 
	thread_exit();
} 

static bool valid_pointer( void * p){
	return ((int*)p != NULL && (int*)p < (int*)PHYS_BASE && (int*)pagedir_get_page(thread_current()->pagedir, p) != NULL); 
}

static bool valid_pointer_to_size( void * p, int size ){
	return ((int*)p != NULL && (int*)p < (int*)PHYS_BASE && (int*)pagedir_get_page(thread_current()->pagedir, p) != NULL)
					&& ( (char*)p+size-1 < (char*)PHYS_BASE && (int*)pagedir_get_page(thread_current()->pagedir, (char*)p+size-1) != NULL   ); 
}


static bool  perform_create(struct intr_frame* f UNUSED){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(char*))){
		bad_parameters_exit();
		return false;  
	}
  const char* filename =(char*) *(((int*)f->esp)+1); 
	if(!valid_pointer_to_size(((int*)f->esp)+2, sizeof(int))){
		bad_parameters_exit();
		return false;  
	}
	unsigned int init_size = * (((int*)f->esp)+2);
	// Checking pointer to file name
	if( filename == NULL ){
		bad_parameters_exit(); 
		return false; 
	}
	// Checking the string
	int i = 0;  
	do {
		if ( !valid_pointer(filename + i) ){
			bad_parameters_exit(); 
			return false; 
		}
		if ( filename[i] == '\0' ){
			//lock_acquire(&syscall_lock); 
			int res = filesys_create(filename, init_size);		 	
			//lock_release(&syscall_lock); 
			return res; 		
		}
	}while( ++i <= MAX_FILE_NAME );
  return false; 
}

static bool correct_file_fd(int fd){
	return fd >= 2 && fd < 130 ;
}

static int perform_open(struct intr_frame *f UNUSED){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(char*))){
		bad_parameters_exit();
		return -1 ;  
	}  
	const char* filename =(char*) *(((int*)f->esp)+1);
	// Checking pointer to file name
	if( filename == NULL ){
		bad_parameters_exit(); 
		return -1; 
	}
	// Checking the string
	int i = 0;  
	do {
		if ( !valid_pointer(filename + i) ){
			bad_parameters_exit(); 
			return -1; 
		}
		// If file name ok, then break and go to filesys_open() ...
		if ( filename[i] == '\0' )
			break; 	 	
	}while( ++i <= MAX_FILE_NAME );
	// File name is too long or no terminator, error 
	if(i > MAX_FILE_NAME )	{
		bad_parameters_exit(); 
  	return -1; 
	}

  struct thread * current_thread = thread_current();
  struct bitmap * bmp = current_thread->available_descriptors;
  struct file ** array = current_thread->file_descriptors;
  /* Check for available file descrptor   */
  size_t fd  = bitmap_scan (bmp, 0, 1, false);
  if (fd == BITMAP_ERROR)
    return -1;
	// Continue, try to open the file... 
	//lock_acquire(&syscall_lock); 
  struct file * fs = filesys_open(filename);
	//lock_release(&syscall_lock);   
	if( fs == NULL )
    return -1;

  array[fd] = fs;
  bitmap_mark(bmp, fd);
  return fd+2; 
}

static void perform_close(struct intr_frame *f UNUSED){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(int))){
		bad_parameters_exit();
		return;  
	}   
	int fd  = * (((int*)f->esp)+1);
	if(!correct_file_fd(fd))
			return;
   struct thread * current_thread = thread_current();
   struct bitmap * bmp = current_thread->available_descriptors;
   struct file ** array = current_thread->file_descriptors;
   /* Test if the file struct exists, and then free it   */ 
  fd -= 2;
 	if(bitmap_test(bmp, fd)){
     bitmap_reset(bmp, fd);
		 //lock_acquire(&syscall_lock);
     file_close(array[fd]);
     //lock_release(&syscall_lock); 
		 array[fd] = NULL;
   }
}

static int perform_read(struct intr_frame *f UNUSED){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(int))){
		bad_parameters_exit();
		return -1;  
	}
  int fd  = * (((int*)f->esp)+1);
	if(!valid_pointer_to_size(((int*)f->esp)+2, sizeof(char*))){
		bad_parameters_exit();
		return -1;  
	}
  char * buffer = (char*) *(((int*)f->esp)+2);
	if(!valid_pointer_to_size(((int*)f->esp)+3, sizeof(unsigned int))){
		bad_parameters_exit();
		return -1;  
	}
  unsigned int size = * (((unsigned int*)f->esp)+3);
  unsigned int sizeSave = size; 
	// Check if buffer is NULL 
	if( buffer == NULL ){
		bad_parameters_exit(); 
		return -1; 
	}
	// Check whether the memory which we write in is valid 
	unsigned int i = 0; 
	while( i++ < size ){
		if ( !valid_pointer(buffer + i) ){
			bad_parameters_exit(); 
			return -1; 
		}
	}

  if(fd == STDIN_FILENO){
    for(; size > 0; size--){
      *buffer  = input_getc();
      buffer += 1;
    }
    return sizeSave; 
  }
	if(!correct_file_fd(fd))
		return -1;
  struct thread * current_thread = thread_current();
  struct bitmap * bmp = current_thread->available_descriptors;
  struct file ** array = current_thread->file_descriptors;
  /* Test if the file struct exists, and then read it   */ 
	fd -= 2;
  if(bitmap_test(bmp, fd)){
		return file_read(array[fd], buffer, size);
	}else{
    return -1;
  }
}

static int perform_write(struct intr_frame *f UNUSED){
  if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(int))){
		bad_parameters_exit();
		return -1;  
	}
  int fd  = * (((int*)f->esp)+1);
	if(!valid_pointer_to_size(((int*)f->esp)+2, sizeof(char*))){
		bad_parameters_exit();
		return -1;  
	}
  char * buffer = (char*) *(((int*)f->esp)+2);
	if(!valid_pointer_to_size(((int*)f->esp)+3, sizeof(unsigned int))){
		bad_parameters_exit();
		return -1;  
	}
  unsigned int size = * (((unsigned int*)f->esp)+3);
	// Check if buffer is NULL 
	if( buffer == NULL ){
		bad_parameters_exit(); 
		return -1; 
	}
	// Check whether the memory which we read from is valid 
	unsigned int i = 0; 
	while( i++ < size ){
		if ( !valid_pointer(buffer + i) ){
			bad_parameters_exit(); 
			return -1; 
		}
	}  

	if(fd == STDOUT_FILENO){
    /* We could split the buffer here if size is too big */
    putbuf(buffer, size);
    return size; 
  }
	if(!correct_file_fd(fd))
				return -1;
  struct thread * current_thread = thread_current();
  struct bitmap * bmp = current_thread->available_descriptors;
  struct file ** array = current_thread->file_descriptors;
  /* Test if the file struct exists, and then free it   */
	fd -= 2; 
  if(bitmap_test(bmp, fd)){
    return file_write(array[fd], buffer, size);
	}else{
    return -1;
  }
}


static void perform_exit(struct intr_frame * f ){
	 if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(int))){
		bad_parameters_exit();
		return;  
	}
  int status  = * (((int*)f->esp)+1); 
	thread_current()->parent->exit_status = status;
	thread_exit();
}

static tid_t perform_exec(struct intr_frame* f ){
	 if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(char*))){
		bad_parameters_exit();
		return -1;  
	}
	char * cmd_line = (char*) *(((int*)f->esp)+1);
	if(cmd_line == NULL){
		bad_parameters_exit(); 
		return -1; 
	}
	int i = 0;
	const int MAX_CMD_LINE = 2048;
	while(i < MAX_CMD_LINE ){
		if ( !valid_pointer(cmd_line + i) ){
			bad_parameters_exit(); 
			return -1; 
		}
		if( cmd_line[i++] == '\0')
			break; 
	}
	if( i >= MAX_CMD_LINE ){
		bad_parameters_exit(); 
		return -1; 
	}
	return process_execute(cmd_line);
}

static int perform_wait(struct intr_frame* f){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(int))){
		bad_parameters_exit();
		return -1;  
	}
	int pid  = * (((int*)f->esp)+1);
	return process_wait(pid);
}

static void perform_seek(struct intr_frame *f){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(int))){
		bad_parameters_exit();
		return;  
	}
  int fd  = * (((int*)f->esp)+1);
	if(!valid_pointer_to_size(((int*)f->esp)+2, sizeof(int))){
		bad_parameters_exit();
		return;  
	}
	int pos  = * (((int*)f->esp)+2);

	if(!correct_file_fd(fd))
		return;

  struct thread * current_thread = thread_current();
  struct bitmap * bmp = current_thread->available_descriptors;
  struct file ** array = current_thread->file_descriptors;
  /* Test if the file struct exists   */ 
	fd -= 2;
  if(bitmap_test(bmp, fd)){
		off_t len = file_length(array[fd]);     
		if( pos > len ){
			file_seek(array[fd], len); 
		}else{
			file_seek(array[fd], pos); 
		}
  }
}

static int perform_tell(struct intr_frame *f){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(int))){
		bad_parameters_exit();
		return -1;  
	}
  int fd  = * (((int*)f->esp)+1);
	if(!correct_file_fd(fd))
		return -1;

  struct thread * current_thread = thread_current();
  struct bitmap * bmp = current_thread->available_descriptors;
  struct file ** array = current_thread->file_descriptors;
  /* Test if the file struct exists   */ 
	fd -= 2;
  if(bitmap_test(bmp, fd)){
		return file_tell(array[fd]); 
  }
	return -1; 
}

static int perform_filesize(struct intr_frame *f){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(int))){
		bad_parameters_exit();
		return -1;  
	}
  int fd  = * (((int*)f->esp)+1);
	if(!correct_file_fd(fd))
		return -1;

  struct thread * current_thread = thread_current();
  struct bitmap * bmp = current_thread->available_descriptors;
  struct file ** array = current_thread->file_descriptors;
  /* Test if the file struct exists   */ 
	fd -= 2;
  if(bitmap_test(bmp, fd)){
		return file_length(array[fd]); 
  }
	return -1; 
}

static bool perform_remove(struct intr_frame *f){
	if(!valid_pointer_to_size(((int*)f->esp)+1, sizeof(char*))){
		bad_parameters_exit();
		return -1 ;  
	}  
	const char* filename =(char*) *(((int*)f->esp)+1);
	// Checking pointer to file name
	if( filename == NULL ){
		bad_parameters_exit(); 
		return -1; 
	}
	// Checking the string
	int i = 0;  
	do {
		if ( !valid_pointer(filename + i) ){
			bad_parameters_exit(); 
			return -1; 
		}
		// If file name ok, then break and go to filesys_remove() ...
		if ( filename[i] == '\0' )
			break; 	 	
	}while( ++i <= MAX_FILE_NAME );
	// File name is too long or no terminator, error 
	if(i > MAX_FILE_NAME )	{
		bad_parameters_exit(); 
  	return -1; 
	}

/*  struct thread * current_thread = thread_current();
  struct bitmap * bmp = current_thread->available_descriptors;
  struct file ** array = current_thread->file_descriptors;
*/  
	// Continue, try to remove the file... 
	//lock_acquire(&syscall_lock); 
  bool success = filesys_remove(filename);
	//lock_release(&syscall_lock);   
	return success;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	if(!valid_pointer(f->esp)){
		bad_parameters_exit();
		return;  
	}

  int syscall_no = *((int*)f->esp);
  switch(syscall_no){
  case SYS_HALT:
    power_off();
    break;
  case SYS_CREATE:
    f->eax = perform_create(f);
    break;
  case SYS_OPEN:
    f->eax = perform_open(f);
    break;
  case SYS_CLOSE:
    perform_close(f);
    break;
  case SYS_READ:
    f->eax = perform_read(f);
    break;
  case SYS_WRITE:
    f->eax = perform_write(f);
    break;
  case SYS_EXIT:
    perform_exit(f);
    break;
	case SYS_EXEC:
		f->eax = perform_exec(f);
		break;
	case SYS_WAIT:
		f->eax = perform_wait(f);
		break;
	case SYS_SEEK:
		perform_seek(f); 
		break; 
	case SYS_TELL:
		f->eax = perform_tell(f); 
		break; 
	case SYS_FILESIZE: 
		f->eax = perform_filesize(f); 
		break; 
	case SYS_REMOVE: 
		f->eax = perform_remove(f); 
		break; 
  default:
		bad_parameters_exit(); 
    break;
  }
}
