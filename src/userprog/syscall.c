#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/console.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool  perform_create(struct intr_frame* f UNUSED){
  const char* filename =(char*) *(((int*)f->esp)+1);
  unsigned int init_size = * (((int*)f->esp)+2);
  return filesys_create(filename, init_size);
}

static bool correct_file_fd(int fd){
	return fd >= 2 && fd < 130 ;
}

static int perform_open(struct intr_frame *f UNUSED){
  struct thread * current_thread = thread_current();
  struct bitmap * bmp = current_thread->available_descriptors;
  struct file ** array = current_thread->file_descriptors;
  /* Check for available file descrptor   */
  size_t fd  = bitmap_scan (bmp, 0, 1, false);
  if (fd == BITMAP_ERROR)
    return -1;
  const char* filename =(char*) *(((int*)f->esp)+1);
  struct file * fs = filesys_open(filename);
  if( fs == NULL )
    return -1;
  array[fd] = fs;
  bitmap_mark(bmp, fd);
  return fd+2; 
}

static void perform_close(struct intr_frame *f UNUSED){
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
     file_close(array[fd]);
     array[fd] = NULL;
   }
}

static int perform_read(struct intr_frame *f UNUSED){
  int fd  = * (((int*)f->esp)+1);
  char * buffer = (char*) *(((int*)f->esp)+2);
  unsigned int size = * (((unsigned int*)f->esp)+3);
  unsigned int sizeSave = size; 
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
  /* Test if the file struct exists, and then free it   */ 
	fd -= 2;
  if(bitmap_test(bmp, fd)){
    return file_read(array[fd], buffer, size);
  }else{
    return -1;
  }
}

static int perform_write(struct intr_frame *f UNUSED){
  int fd  = * (((int*)f->esp)+1);
  char * buffer = (char*) *(((int*)f->esp)+2);
  unsigned int size = * (((unsigned int*)f->esp)+3);
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
  //int status  = * (((int*)f->esp)+1); 
	 thread_exit();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
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
  default:
    thread_exit();
    break;
  }
}
