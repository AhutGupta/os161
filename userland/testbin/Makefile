#
# Makefile for src/testbin (sources for programs installed in /testbin)
#

TOP=../..
.include "$(TOP)/mk/os161.config.mk"

SUBDIRS=add argtest badcall bigexec bigfile bigfork bigseek bloat conman \
	crash ctest dirconc dirseek dirtest f_test factorial farm faulter \
	filetest fileonlytest forkbomb forktest frack guzzle hash hog huge kitchen \
	malloctest matmult multiexec palin parallelvm poisondisk psort \
	quinthuge quintmat quintsort randcall redirect rmdirtest rmtest \
	sbrktest schedpong shll sink sort sparsefile spinner sty tail tictac \
	triplehuge triplemat triplesort usemtest waiter zero \
<<<<<<< HEAD
	consoletest shelltest opentest readwritetest closetest \
	mytest

=======
	consoletest shelltest opentest readwritetest closetest stacktest
>>>>>>> 160b83330ef0215827c53cf655bfdb8a4e1480ea

# But not:
#    userthreads    (no support in kernel API in base system)

.include "$(TOP)/mk/os161.subdir.mk"
