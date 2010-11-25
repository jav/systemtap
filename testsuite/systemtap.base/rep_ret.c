// Use -fomit-frame-pointer so this doesn't blow the stack
void rep_ret() { asm("rep; ret"); } // uprobes allowed
void repnz_ret() { asm("repnz; ret"); } // NOT allowed
int main() { rep_ret(); repnz_ret(); return 0; }
