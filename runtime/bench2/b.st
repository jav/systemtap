# printf execname

# You can put initialization in here
probe begin {
	printf("FOO\n")
}

# This gets probed millions of times. TEST gets replaces
# by the real probe point.
probe TEST {
	printf("%s\n", execname())
}

probe end {
	printf("DONE\n")
}
