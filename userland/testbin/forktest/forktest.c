/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * forktest - test fork().
 *
 * This should work correctly when fork is implemented.
 *
 * It should also continue to work after subsequent assignments, most
 * notably after implementing the virtual memory system.
 */

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

	if(pid == 0){
		exit(0);
		//kprintf("Child");
		write(2, "I am the child\n", 1);
	} else {
		returnValue = waitpid(pid, &x, 0);
		if(returnValue == 0){
		write(2, "I am the parent\n", 1);
		}
	}
} 


