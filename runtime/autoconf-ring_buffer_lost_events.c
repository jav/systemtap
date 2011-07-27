/* Some kernels have place holder recording of dropped events in the
   ring_buffer peek and consume calls. */
#include <linux/types.h>
#include <linux/ring_buffer.h>

void foo (void)
{
  struct ring_buffer_event *event;
  /* last field is not always there */
  event = ring_buffer_peek(NULL, 1, NULL, NULL);
}
