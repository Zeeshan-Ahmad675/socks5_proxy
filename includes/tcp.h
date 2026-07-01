#ifndef TCP_H
#define TCP_H

#include "fd_util.h"
#include "socks.h"

#define TCP_INPROGRESS 0x09
#define TCP_EAGAIN -1
#define TCP_SUCCESS 0x00
#define TCP_SERVER_ERROR 0x01
#define TCP_NETWORK_UNREACHABLE 0x03
#define TCP_HOST_UNREACHABLE 0x04
#define TCP_CONNECTION_REFUSED 0x05
#define TCP_ATYP_UNSUPPORTED 0x08

File_Descriptor start_tcp_server(const char* port, int max_queued);
int open_new_connection_to_target(SOCKS_Client& client);

int check_inprogress_connection(const File_Descriptor* fd);

#endif