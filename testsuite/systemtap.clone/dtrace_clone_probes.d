provider test {
	 probe main(pid_t pid);
	 probe main2();
	 probe child1(pid_t pid);
	 probe child1_pid(pid_t pid);
	 probe child2(pid_t pid);
	 probe child2_pid(pid_t pid);
};
