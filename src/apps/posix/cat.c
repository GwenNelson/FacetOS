#include <fcntl.h>
#include <unistd.h>
int main(int argc, char **argv) { char b[128]; if (argc < 2) return 1; for (int i=1;i<argc;i++) { int fd=open(argv[i],O_RDONLY); if(fd<0) return 1; ssize_t n; while((n=read(fd,b,sizeof(b)))>0) if(write(1,b,n)!=n) return 1; close(fd); } return 0; }
