# simple variable

global a

# You can put initialization in here
probe begin {
	a = 1
}

# This gets probed millions of times. TEST gets replaces
# by the real probe point.
probe TEST {
	if (a == 0)
		printf("a is ZERO\n")
}

# something rarely called. Just to force the optimizer
# to not optimize access to "a"
probe kernel.function("sys_delete_module")
{
	a = 2
}

probe end {
	printf("a=%d\n", a)
}

