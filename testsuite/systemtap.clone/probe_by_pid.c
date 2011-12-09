#include <stdio.h>
#include <unistd.h>

void test_function(void)
{
    sleep(1);
    (void) getpid();
}

int main(void)
{
    while (1) {
	test_function();
    }
}
