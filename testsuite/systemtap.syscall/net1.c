/* COVERAGE: socket fcntl fcntl64 bind listen accept */
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>

int main()
{
  struct sockaddr_in sa;
  int flags, listenfd, cfd;
  

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  // socket (PF_INET, SOCK_STREAM, 0) = NNNN

  flags = fcntl(listenfd, F_GETFL, 0);
  // fcntl[64]* (NNNN, F_GETFL, 0x[0]+) = NNNN
  fcntl(listenfd, F_SETFL, flags | O_NONBLOCK);
  // fcntl[64]* (NNNN, F_SETFL, XXXX) = 0

  bzero(&sa, sizeof(sa));
  sa.sin_family=AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_ANY);
  sa.sin_port = htons(8765);

  bind(listenfd, (struct sockaddr *)&sa, sizeof(sa));
  // bind (NNNN, {AF_INET, 0.0.0.0, 8765}, 16) = 0

  listen (listenfd, 7);
  // listen (NNNN, 7) = 0

  cfd = accept(listenfd, (struct sockaddr *)NULL, NULL);
  // accept (NNNN, 0x[0]+, 0x[0]+) = -NNNN (EAGAIN)

  close(cfd);
  close(listenfd);
  return 0;
}
