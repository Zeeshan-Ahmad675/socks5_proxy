#ifndef SOCKS_H
#define SOCKS_H

#include "fd_util.h"
#include <cstdint> // uint8_t, uint32_t
#include <sys/socket.h> // sockaddr, socklen_t

#define S5E_FATAL 1
#define S5E_RDHUP 2
#define S5E_WRHUP 4
#define S5E_RDAGAIN 8
#define S5E_WRAGAIN 16

enum SOCKS_Returns: uint8_t
{
    S5_SUCCESS = 129,
    S5_EAGAIN,
    S5_RDAGAIN,
    S5_WRAGAIN,
    S5_ADD_TARGET_CONNECT_TIMER,
    S5_ADD_TARGET_TO_EPOLL,
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
    COMMUNICATION,
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
    uint8_t _error;
    bool _connected_to_target;

    const SOCKS_Returns abstracted_read(size_t pre_read, size_t count, bool& read_avail);
    const SOCKS_Returns abstracted_write(const uint8_t* set_init_buf, int init_count, size_t count, bool& write_avail, bool close);
public:
    File_Descriptor target_host_fd;
    Timer_File_Descriptor* timer;
    uint8_t notable_events;
    uint8_t err;

    SOCKS_Client(int fd, int flags, sockaddr addr, socklen_t addrlen);
    const File_Descriptor& get_file_descriptor() const { return _fd; };
    const uint8_t* get_target_host_address_and_port() const; 
    bool set_bind_address_and_port(const sockaddr* bind_addr, const socklen_t addrlen);
    const SOCKS_Returns handle_request(uint32_t events);
    const SOCKS_Returns check_target_connection(bool using_var);

    ~SOCKS_Client();

};



#endif