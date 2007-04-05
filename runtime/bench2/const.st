# constant variable

# This is a test of the translator's ability to detect
# when variables are never modified and to optimize 
# reading of them.

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

# something rarely called. Just here to be
# as close as possible to "var.st".
probe kernel.function("sys_delete_module")
{
}

probe end {
	printf("a=%d\n", a)
}

