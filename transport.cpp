#include "transport.h"

#include <stdio.h>
#include <string.h> // memset
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <cstdarg>

namespace Transport {

/* The data is segmented as:
 * [private header]||[user header]||[user data]
 * where || denotes concatenation, not a delimiter.
 */

/* The private header is:
 * [type of data]||[length of data]
 * where type of data and the length are both 4 byte unsigned integers.
 */
#define PRIVATE_HEADER_LEN  8

enum data_type {
  generic_data,
  header_length_update
};

#define GET_PRIVATE_HEADER(type, len, buf) type = ((uint32_t *)(buf))[0] ; \
                                           len = ((uint32_t *)(buf))[1]

void Connection::log(const char *format, ...) {
  if (logging_on) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
  }
}

Connection::Connection(const char *addr, int port, bool strict, bool logging) {
  header_size = PRIVATE_HEADER_LEN;
  logging_on = logging;

  if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    log("[error] Couldn't connect to socket.\n");
    return;
  }

  // First try to connect to the address provided.
  memset(&remote_addr, '0', sizeof(remote_addr));
  remote_addr.sin_family = AF_INET;
  remote_addr.sin_port = htons(port);

  // No address will put the connection immediately in listening mode.
  if (!addr) {
    log("[info] Entering listening mode by default.\n");
    goto listen;
  }

  if (inet_pton(AF_INET, addr, &remote_addr.sin_addr) <= 0) {
    log("[warning] Could not resolve address, listening for a peer.\n");
    goto listen;
  }

  if (connect(socket_fd, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
    log("[warning] Could not connect, listening for a peer.\n");
    goto listen;
  }
  log("[connected] %s\n", addr);
  return;

listen:
  log("[listening] Waiting on connection.\n");

  // If that didn't work, listen for a connection.
  remote_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(socket_fd, (struct sockaddr*)&remote_addr, sizeof(remote_addr));
  listen(socket_fd, 10);
  while (1) {
    struct sockaddr_in new_addr;
    socklen_t new_addr_len;
    int new_fd = accept(socket_fd, (struct sockaddr*)&new_addr, &new_addr_len);
    char new_addr_name[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET, &(new_addr.sin_addr), new_addr_name, sizeof(new_addr_name));

    // Check that the connection we accept is the one we want.
    if (strict && addr && strncmp(new_addr_name, addr, INET6_ADDRSTRLEN)) {
      fprintf(stderr, "Invalid connection from %s.\n", new_addr_name);
      close(new_fd);
    } else {
      socket_fd = new_fd;
      memcpy(&remote_addr, &new_addr, sizeof(new_addr));
      log("[connected] %s\n", new_addr_name);
      break;
    }
  }
}
void Connection::setHeaderSize(uint32_t size) {
  // Update the receiving party to use this header size.
  uint32_t tmp_write_buffer[2];
  tmp_write_buffer[0] = header_length_update;
  tmp_write_buffer[1] = size + PRIVATE_HEADER_LEN;
  write(socket_fd, tmp_write_buffer, sizeof(tmp_write_buffer));
}

void Connection::sendData(void *data, uint32_t len, void *header_data) {
  // Allocate a temporary buffer with room for the header. 
  void *tmp_write_buffer = malloc(len + header_size);
  ((int *)tmp_write_buffer)[0] = len;
  memcpy(&(((int *)tmp_write_buffer)[1]), data, len);
  write(socket_fd, tmp_write_buffer, len + header_size);
  free(tmp_write_buffer);
}

void *Connection::_recvData(uint32_t *out_len, void *header_data) {
  char recv_buffer[1024];
  *out_len = 0; // Indicates error.

  void *heap_data;
  int n;

  log("[listening] Waiting on data.\n");
  while ((n = read(socket_fd, recv_buffer, sizeof(recv_buffer))) <= 0) {
    // Wait for some data to come.
  }
  uint32_t position = n - header_size;

  // Parse out private header
  uint32_t type, len;
  GET_PRIVATE_HEADER(type, len, recv_buffer);

  switch (type) {
    case generic_data:
      break;
    case header_length_update:
      if (len >= PRIVATE_HEADER_LEN) {
        header_size = len;
        *out_len = (uint32_t)-1; // Indicates non-error.
      }
    default:
      return nullptr;
  }

  if (n < 0) {
    log("[error] Could not read from socket.\n");
    return nullptr;
  } else {
    heap_data = calloc(1, len);
    memcpy(heap_data, &(((int *)recv_buffer)[1]), n - header_size);
  }
  
  if (n >= len + header_size) {
    fprintf(stderr, "Length was less than a second call to recv\n");
    goto done;
  }

  while ((n = read(socket_fd, &(((char *)heap_data)[position]), len + header_size)) > 0) {
    position += n;
    if (position >= len) {
      //fprintf(stderr, "Length exceeded expectations. %d vs %d\n", len, position);
      goto done;
    }
  }

  if (n < 0) {
    log("[error] Could not receive data.\n");
    free(heap_data);
    return nullptr;
  }

done:
  *out_len = position;//len;
  return heap_data;
}

void *Connection::recvData(uint32_t *out_len, void *header_data) {
  void *out_data = nullptr;

  while (1) {
    out_data = _recvData(out_len, header_data);
    // If we just handled something private.
    if (!out_data && *out_len == (uint32_t)-1) {
      continue;
    }
    break;
  }

  return out_data;
}

} // Transport

