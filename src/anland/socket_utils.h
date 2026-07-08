#ifndef DISPLAY_SOCKET_UTILS_H
#define DISPLAY_SOCKET_UTILS_H

#include <stddef.h>

int send_fds(int sock, const void *data, size_t data_len,
             const int *fds, int fd_count);

int recv_fds(int sock, void *data, size_t data_len,
             int *fds, int fd_count, int *fds_received);

int connect_unix(const char *path);

int send_all(int fd, const void *buf, size_t len);

int recv_all(int fd, void *buf, size_t len);

#endif
