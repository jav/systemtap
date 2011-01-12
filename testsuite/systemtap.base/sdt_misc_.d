provider sdt_misc {
        probe test_probe_0 ();
	probe test_probe_1 (int i);
	probe test_probe_2 (int i);
	probe test_probe_3 (int i, char* x);
	probe test_probe_4 (struct astruct arg);
};
struct astruct {int a; int b;};
