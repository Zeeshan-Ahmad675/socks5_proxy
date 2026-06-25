#ifndef SOCKS5_H
#define SOCKS5_H

#include "fd_util.h"
#include <cstdint> // uint8_t, uint32_t
#include <sys/socket.h> // sockaddr, socklen_t

enum SOCKS_Returns
{
    S5_SUCCESS,
    S5_EAGAIN,
    S5_ADD_TIMER,
    S5_ADD_TO_EPOLL,
    S5_REMOVE_EPOLLIN,
    S5_CONNECTION_CLOSED,
    S5_BAD_REQUEST,
};

enum SOCKS_State 
{
    NONE = 0,
    INITIAL,
    AUTHENTICATION,
    REQUESTS,
    EVALUATION_AND_REPLY,
    CLOSED
};

class SOCKS_Client {
private:
    SOCKS_State _state, _repeat_state;
    uint8_t _buffer[1024];
    int _buff_ptr_in, _buff_ptr_out;
    sockaddr _network_addr;
    socklen_t _addrlen;
    File_Descriptor _fd;

    const SOCKS_Returns abstracted_read(size_t pre_read, size_t count, uint32_t& read_avail);
    const SOCKS_Returns abstracted_write(const uint8_t* set_init_buf, int init_count, size_t count, uint32_t& write_avail, bool close);
public:
    File_Descriptor target_host_fd;

    SOCKS_Client(int fd, int flags, sockaddr addr, socklen_t addrlen);
    const File_Descriptor& get_file_descriptor() const { return _fd; };
    const uint8_t* get_target_host_address_and_port() const; 
    bool set_bind_address_and_port(const sockaddr* bind_addr, const socklen_t addrlen);
    const SOCKS_Returns handle_request(uint32_t events);

    ~SOCKS_Client();

};



#endif