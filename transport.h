#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstddef>

namespace Transport {



class Connection {
 public:
  Connection(const char *addr, int port, bool strict = false, bool logging = false);

  void sendData(void *data, uint32_t len, void *header_data = nullptr);

  void *recvData(uint32_t *len, void *header_data = nullptr);

  void setHeaderSize(uint32_t size);

 private:
  int socket_fd;
  int port;
  struct sockaddr_in remote_addr;
  uint32_t header_size;
  void *_recvData(uint32_t *out_len, void *header_data);
  void log(const char *format, ...);
  bool logging_on;
};

} // Transport
