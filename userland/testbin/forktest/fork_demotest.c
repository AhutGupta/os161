int x = 0;
int returnValue;
pid = dofork();

if(pid == 0){
	exit(0);
	kprintf("Child");
} else {
	returnValue = waitpid(pid, &x, 0);
	if(returnValue == 0){
		kprintf("Success");
	}
}