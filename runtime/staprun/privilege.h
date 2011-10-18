#ifndef PRIVILEGE_H
#define PRIVILEGE_H

enum privilege_t {
  // Define the privilege levels in order where a higher value indicates a higher privilege level.
  // NOTE:
  //   - Values must be a power of 2 between 0x1 and 0x40000000 inclusive, since they will be used
  //     to form a bit mask of type int must be remain non-negative.
  //   - These values cannot change in the future because they will be encoded into the generated
  //     modules in order to identify their required privilege level now and in the future. So
  //     leave space around each for possible future intermediate privilege levels.
  pr_stapusr = 0x00000080,  // A very low privilege level
  pr_stapdev = 0x01000000,  // A very high privilege level
  // These are used for iterating */
  pr_end,
  pr_begin = pr_stapusr,
  pr_all = pr_stapusr | pr_stapdev
};

#endif // PRIVILEGE_H
