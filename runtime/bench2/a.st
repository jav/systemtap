# printf probefunc

# You can put initialization in here
probe begin {
	printf("BEGIN\n")
}

# This gets probed millions of times. TEST gets replaces
# by the real probe point.
probe TEST {
	printf("%s\n",probefunc())
}

