// Based on RFC 1928 (SOCKS Protocol Version 5)



// Should be some method through which we can remove the overhead of re-evaluatint the same code in case of S5_EAGAIN.

#include "socks.h"
#include "tcp.h"
#include <sys/epoll.h> // EPOLLET, EPOLLOUT
#include <netinet/in.h> // ntohs()
#include <cstring>  // memset()
#include <iostream> // cerr

#define SOCKS_READ(pre_read, to_read, read_avail) \
    do { \
        if (abstracted_read(pre_read, to_read, read_avail) == S5_SUCCESS); \
        else return S5_EAGAIN; \
    } while(0)

#define SOCKS_NONCLOSE_WRITE(set_init_buf, init_count, count, write_avail) \
    do { \
        if (abstracted_write(set_init_buf, init_count, count, write_avail, false) == S5_SUCCESS); \
        else return S5_EAGAIN; \
    } while(0)


const SOCKS_Returns SOCKS_Client::abstracted_read(size_t pre_read, size_t to_read, uint32_t& read_avail)
{
    ssize_t nbytes;
    uint8_t* local_buff = new uint8_t[1024];

    nbytes = _fd.nonblocking_read(local_buff, pre_read + to_read - _buff_ptr_in, read_avail);
    // Needs to handle the invalid nbytes returned when an errors occurs

    memcpy(_buffer + _buff_ptr_in, local_buff, nbytes);
    _buff_ptr_in += nbytes;
    delete[] local_buff;

    if (_buff_ptr_in != pre_read + to_read)
        return S5_EAGAIN;
    return S5_SUCCESS;
}


const SOCKS_Returns SOCKS_Client::abstracted_write(const uint8_t* set_init_buf, int init_count, size_t count, uint32_t& write_avail, bool close)
{
    ssize_t nbytes;

    if (_buff_ptr_out < init_count && write_avail) 
    {
        nbytes = _fd.nonblocking_write(set_init_buf + _buff_ptr_out, init_count - _buff_ptr_out, write_avail);
        _buff_ptr_out += nbytes;
    }
    if (_buff_ptr_out >= init_count && write_avail)
    {
        uint8_t* local_buff = new uint8_t[1024];
        memcpy(local_buff, _buffer + _buff_ptr_out, count - _buff_ptr_out);
        nbytes = _fd.nonblocking_write(local_buff, count - _buff_ptr_out, write_avail);
        _buff_ptr_out += nbytes;
        // Needs to handle the invalid nbytes returned when an errors occurs

        delete[] local_buff;
    }

    if (_buff_ptr_out != count)
    {
        _repeat_state = _state;
        return (close ? S5_REMOVE_EPOLLIN: S5_EAGAIN);  // If this returns in case of close, then how would we make sure the connection is closed within 10 seconds as reuqired
    }
    if (close)
    {
        _state = CLOSED;
        return S5_CONNECTION_CLOSED;
    }
    _buff_ptr_out = 0;
    return S5_SUCCESS;
}





SOCKS_Client::SOCKS_Client(int fd, int flags, sockaddr addr, socklen_t addrlen) : _state (INITIAL), _repeat_state(NONE), _buff_ptr_in (0), _buff_ptr_out (0), _network_addr(addr), _addrlen(addrlen), _fd(fd), target_host_fd(-1)
{
    memset(_buffer, 0, sizeof(_buffer));
    this->_fd.add_status_flags(flags);
}

const uint8_t* SOCKS_Client::get_target_host_address_and_port() const
{
    if (!(_state == EVALUATION_AND_REPLY)) return nullptr;

    switch (_buffer[3]) {
        case 0x01:
        {
            uint8_t *addr_port = new uint8_t[7];
            memcpy(addr_port, _buffer + 3, 7);
            return addr_port;
        }
        case 0x03:
        {
            uint8_t *addr_port = new uint8_t[_buffer[4] + 4];
            memcpy(addr_port, _buffer + 3, _buffer[4] + 4);
            return addr_port;
        }
        case 0x04:
        {
            uint8_t *addr_port = new uint8_t[19];
            memcpy(addr_port, _buffer + 3, 19);
            return addr_port;
        }
        default:
            return nullptr;
    }
}

bool SOCKS_Client::set_bind_address_and_port(const sockaddr* bind_addr, const socklen_t addrlen)
{
    if (!(_state == EVALUATION_AND_REPLY)) return false;

    if (bind_addr->sa_family == AF_INET)
    {
        _buffer[3] = 0x01;
        in_port_t port = ((sockaddr_in*)bind_addr)->sin_port;
        in_addr_t ip_addr = ((sockaddr_in*)bind_addr)->sin_addr.s_addr;

        memcpy(_buffer + 4, &ip_addr, 4);
        memcpy(_buffer + 8, &port, 2);
        return true;
    }
    else if (bind_addr->sa_family == AF_INET6)
    {
        _buffer[3] = 0x04;
        in_port_t port = ((sockaddr_in6*)bind_addr)->sin6_port;
        uint8_t* ip_addr = ((sockaddr_in6*)bind_addr)->sin6_addr.s6_addr;

        memcpy(_buffer + 4, &ip_addr, 16);
        memcpy(_buffer + 20, &port, 2);
        return true;
    }
    else return false;
}

const SOCKS_Returns SOCKS_Client::handle_request(const uint32_t events) {
    if (_state == CLOSED)
        return S5_CONNECTION_CLOSED;

    ssize_t nbytes;
    size_t to_read;
    uint32_t read_avail = events & EPOLLIN;
    uint32_t write_avail = events & EPOLLOUT;
    if (!read_avail && !write_avail) return S5_EAGAIN;

    // What would happen when the state machine doens't need EPOLLIN right now rather EPOLLOUT but EPOLLIN is available, then how to delegate it or vice versa

    if (_state == INITIAL)
    {
        if (_buff_ptr_in < 2)
            SOCKS_READ(0, 2, read_avail);
        if (_buff_ptr_in == 2) 
        {
            if (_buffer[0] != 0x05)
            {
                std::cerr << "SOCKS_Client: INITIAL: Unsupported version" << std::endl;
                uint8_t init_buff[2] = { 0x05, 0xFF };
                return abstracted_write(init_buff, 2, 2, write_avail, true);
            }
        }
        if (_buff_ptr_in > 2 && _buff_ptr_in < 2 + _buffer[1])
            SOCKS_READ(2, _buffer[1], read_avail);
    }
    if (write_avail && _buff_ptr_in == 2 + _buffer[1])
    {
        int i;
        for(i = 2; i < 2 + _buffer[1]; i++)
        {
            uint8_t method = _buffer[i];
            if (method == 0)
            {
                _buffer[1] = 0x00; // No Authentication
                break;
            }
            else if (method == 1)
            {
                _buffer[1] = 0x01; // GSSAPI
                break;
            }
            else if (method == 2)
            {
                _buffer[1] = 0x02; // USERNAME / PASSWORD
                break;
            }
        }
        if (i == 2 + _buffer[1])
        {
            std::cerr << "SOCKS_Client: INITIAL: No supported authentication method" << std::endl;
            uint8_t init_buff[2] = { 0x05, 0xFF };
            return abstracted_write(init_buff, 2, 2, write_avail, true);
        }

        SOCKS_NONCLOSE_WRITE(NULL, 0, 2, write_avail);
        _state = AUTHENTICATION;
    }
    
    if (_state == AUTHENTICATION)
    {
        // No authentication
        _buff_ptr_in = 0;
        _state = REQUESTS; 
    }

    if (_state == REQUESTS)
    {
        _buff_ptr_in = 0; // Just for now. if we can find a better way later, remove it

        if (_buff_ptr_in < 4)
            SOCKS_READ(0, 4, read_avail);
        if (_buff_ptr_in == 4) {
            if (_buffer[0] != 0x05)
            {
                std::cerr << "SOCKS_Client: REQUESTS: Unsupported version" << std::endl;
                uint8_t init_buff[4] = { 0x05, 0x02, 0x00, 0x01 }; // connection not allowed by ruleset
                return abstracted_write(init_buff, 4, 10, write_avail, true);
            }
            if (_buffer[1] != 0x01 || _buffer[1] != 0x02 || _buffer[1] != 0x03)
            {
                std::cerr << "SOCKS_Client: REQUESTS: Unknown command" << std::endl;
                uint8_t init_buff[4] = { 0x05, 0x07, 0x00, 0x01 }; // Command not supported
                return abstracted_write(init_buff, 4, 10, write_avail, true);
            }
            if (_buffer[2] != 0x00)
            {
                std::cerr << "SOCKS_Client: REQUESTS: Reserved byte used" << std::endl;
                uint8_t init_buff[4] = { 0x05, 0x02, 0x00, 0x01 }; // connection not allowed by ruleset
                return abstracted_write(init_buff, 4, 10, write_avail, true);
            }
            if (_buffer[3] != 0x01 || _buffer[3] != 0x03 || _buffer[3] != 0x04)
            {
                std::cerr << "SOCKS_Client: REQUESTS: Unknown Address type" << std::endl;
                uint8_t init_buff[4] = { 0x05, 0x08, 0x00, 0x01 }; // Address type not supported
                return abstracted_write(init_buff, 4, 10, write_avail, true);
            }
        }
        if (_buff_ptr_in >= 4)
        {
            if (_buffer[3] == 0x01)
                SOCKS_READ(4, 6, read_avail);
            else if (_buffer[3] == 0x03)
            {
                if (_buff_ptr_in == 4)
                    SOCKS_READ(4, 1, read_avail);
                if (_buff_ptr_in >= 5)
                    SOCKS_READ(5, _buffer[4] + 2, read_avail);
            }
            else 
                SOCKS_READ(4, 18, read_avail);
            _state = EVALUATION_AND_REPLY;
        }
    }

    if (_state == EVALUATION_AND_REPLY)
    {
        // Currently only supports CONNECT
        if (_repeat_state!= EVALUATION_AND_REPLY)
            _buffer[1] = (uint8_t)open_new_connection_to_target(*this);

        uint8_t init_buff[4] = { 0x05, _buffer[1], 0x00, 0x01 };

        switch (_buffer[0]) {
            case TCP_INPROGRESS:
                return S5_ADD_TIMER;
            case TCP_SUCCESS:
                
                return S5_ADD_TO_EPOLL;
            case TCP_EAGAIN:
                return S5_EAGAIN;
            default:
                return abstracted_write(init_buff, 4, 10, write_avail, true);
        }
    }
}