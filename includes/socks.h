#ifndef SOCKS_H
#define SOCKS_H

#include "fd_util.h"
#include <cstdint> // uint8_t, uint32_t
#include <sys/socket.h> // sockaddr, socklen_t


enum SOCKS_Events: uint8_t
{
    S5E_FATAL = 1,          // No going back
    S5E_RDHUP = 2,          // We can't read after the next EAGAIN on read() from this socket. This is either registered using the EPOLLRDHUP flag of epoll functions or if read() returns 0
    S5E_WRHUP = 4,          // We can't  write on this socket now
    S5E_RDNEED = 8,         // We haven't finished reading this time, so there is still some data to be read
                            // In case of COMMUNICATION (pipe/splice), this also mean that there might be some data to be read from socket to pipe
    S5E_WRNEED = 16,        // We haven't finished writing this time, so there is still some data to be written
                            // In case of COMMUNICATION (pipe/splice), this also mean that there might be some data in the pipe to be written to the socket
    S5E_IN = 32,            // There is some data available to be read
    S5E_OUT = 64,           // We can write to this socket
    S5E_CLOSED = 128        // This socket is closed and is no longer valid to read or write
};

enum SOCKS_Returns: uint8_t
{
    S5_SUCCESS = 129,               // Success in the task on hand
    S5_EAGAIN,                      // Wait for the event to get triggered AGAIN
    S5_RDAGAIN,                     // We are just stuck due to internal memory (mostly) errors, try reading again (other reasons also exist in COMMUNICATION state)
    S5_WRAGAIN,                     // We are just stuck due to internal memory (mostly) errors, try writing again
    S5_ADD_TARGET_CONNECT_TIMER,
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
    Pipe* _cpipe;
    Pipe* _hpipe;
    uint8_t _error;
    bool _connected_to_target;

    const SOCKS_Returns abstracted_read(size_t pre_read, size_t count);
    const SOCKS_Returns abstracted_write(const uint8_t* set_init_buf, int init_count, size_t count, bool close);
    SOCKS_Returns setup_communication();
public:
    File_Descriptor* target_host_fd;
    Timer_File_Descriptor* timer;
    uint8_t cevents;
    uint8_t hevents;

    SOCKS_Client(int fd, int flags, sockaddr addr, socklen_t addrlen);
    const File_Descriptor& get_file_descriptor() const { return _fd; };
    const uint8_t* get_target_host_address_and_port() const; 
    bool set_bind_address_and_port(const sockaddr* bind_addr, const socklen_t addrlen);
    void register_events(const uint32_t events, bool client);
    const SOCKS_Returns handle_request();
    const SOCKS_Returns handle_communication(const bool with_client, const bool read);
    const SOCKS_Returns check_target_connection(const bool using_var);

    ~SOCKS_Client();

};



#endif