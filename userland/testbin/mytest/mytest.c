#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <test161/test161.h>

//#define FORKTEST_FILENAME_BASE "forktest"
static int dofork(void)
{
		int id;
		id = fork();
		if (id < 0) {
			warn("fork");
		}
		return id;
}

int
main(int argc, char *argv[])
{
	 /*static const char expected[] =
		"|----------------------------|\n";
	int nowait=0;

	if (argc==2 && !strcmp(argv[1], "-w")) {
		nowait=1;
	}
	else if (argc!=1 && argc!=0) {
		warnx("usage: forktest [-w]");
		return 1;
	}
	warnx("Starting. Expect this many:");
	write(STDERR_FILENO, expected, strlen(expected));

	test(nowait);

	warnx("Complete.");
	return 0; */

	(void) argc;
	(void) argv;
	int x = 0;
	int returnValue;
	int pid = dofork();
	// int pid = 0;

	if(pid == 0){
		// exit(0);
		printf("Child\n");
		// write(2, "I am the child\n", 1);
		exit(0);
	} else {
		// write(2, "I am the parent outside\n", 1);
		printf("Parent\n");
		returnValue = waitpid(pid, &x, 0);
		if(returnValue == 0){
			// write(2, "I am the parent\n", 1);
			printf("Parent inside");
		}
	}
} 