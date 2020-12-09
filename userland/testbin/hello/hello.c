#include <stdio.h>
#include <unistd.h>

int
main(void)
{
	size_t i=0;
	while(i<65535){
		i++;
	}
	printf("my pid: %d\n", getpid());
	return 0;
}