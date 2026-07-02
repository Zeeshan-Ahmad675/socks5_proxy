// Based on RFC 1928 (SOCKS Protocol Version 5)

// Should be some method through which we can remove the overhead of re-evaluating the same code in case of S5_EAGAIN
#undef _GNU_SOURCE
#define _GNU_SOURCE

#include "socks.h"
#include "tcp.h"
#include <sys/epoll.h> // EPOLL* events and flags
#include <netinet/in.h> // ntohs()
#include <unistd.h> // pipe()
#include <fcntl.h> // O_* flags, splice()
#include <cstring>  // memset(), strerror()
#include <iostream> // cerr



#define c_read_avail (cevents & S5E_IN)
#define c_write_avail (cevents & S5E_OUT)
#define h_read_avail (hevents & S5E_IN)
#define h_write_avail (hevents & S5E_OUT)


#define SOCKS_READ(pre_read, to_read) \
    do { \
        SOCKS_Returns read_ret = abstracted_read(pre_read, to_read); \
        if (read_ret == S5_SUCCESS); \
        else \
            return read_ret; \
    } while(0)

#define SOCKS_WRITE(set_init_buf, init_count, count, close) \
    do { \
        SOCKS_Returns write_ret = abstracted_write(set_init_buf, init_count, count, close); \
        if (write_ret == S5_SUCCESS); \
        else \
            return write_ret; \
    } while(0)


const SOCKS_Returns SOCKS_Client::abstracted_read(size_t pre_read, size_t to_read)
{
    if (_buff_ptr_in >= pre_read)
    {
        int err;
        uint8_t* local_buff = new uint8_t[1024];
        err = _fd.nonblocking_read(local_buff, pre_read + to_read - _buff_ptr_in);

        memcpy(_buffer + _buff_ptr_in, local_buff, _fd.get_last_read());
        _buff_ptr_in += _fd.get_last_read();
        delete[] local_buff;

        switch (err) {
            case 0:
                break;
            case EAGAIN:    // Same value as EWOULDBLOCK on Linux only, so not portable
                _repeat_state = _state;
                cevents |= S5E_RDNEED;
                cevents &= ~S5E_IN;
                return S5_EAGAIN;
            default:
                std::cout << strerror(err) << std::endl;
                _state = CLOSED;
                return S5_CONNECTION_CLOSED; 
        }
    }
    if (cevents & S5E_RDHUP)
    {
        _state = CLOSED;
        return S5_CONNECTION_CLOSED;
    }
    cevents &= ~S5E_RDNEED;
    return S5_SUCCESS;
}


const SOCKS_Returns SOCKS_Client::abstracted_write(const uint8_t* set_init_buf, int init_count, size_t count, bool close)
{
    if (cevents & S5E_WRHUP)
    {
        _state = CLOSED;
        return S5_CONNECTION_CLOSED;
    }

    int err;
    if (_buff_ptr_out < init_count)
    {
        err = _fd.nonblocking_write(set_init_buf + _buff_ptr_out, init_count - _buff_ptr_out);
        _buff_ptr_out += _fd.get_last_write();

        switch (err) {
            case 0:
                break;
            case EAGAIN:     // Same value as EWOULDBLOCK in Linux only, so not portable
                cevents |= S5E_WRNEED;
                cevents &= ~S5E_OUT;
                _repeat_state = _state;
                return S5_EAGAIN;
            case EDQUOT:
            case ENOSPC:
                std::cout << strerror(err) << std::endl;
                cevents |= S5E_WRNEED;
                _repeat_state = _state;
                return S5_WRAGAIN;
            default:
                std::cout << strerror(err) << std::endl;
                _state = CLOSED;
                return S5_CONNECTION_CLOSED; 
        }
    }
    if (_buff_ptr_out >= init_count)
    {
        uint8_t* local_buff = new uint8_t[1024];
        memcpy(local_buff, _buffer + _buff_ptr_out, count - _buff_ptr_out);
        err = _fd.nonblocking_write(local_buff, count - _buff_ptr_out);
        _buff_ptr_out += _fd.get_last_write();
        delete[] local_buff;

        switch (err) {
            case 0:
                break;
            case EAGAIN:
                cevents |= S5E_WRNEED;
                cevents &= ~S5E_OUT;
                _repeat_state = _state;
                return S5_EAGAIN;
            case EDQUOT:
            case ENOSPC:
                std::cout << strerror(err) << std::endl;
                cevents |= S5E_WRNEED;
                _repeat_state = _state;
                return S5_WRAGAIN;
            default:
                std::cout << strerror(err) << std::endl;
                _state = CLOSED;
                return S5_CONNECTION_CLOSED; 
        }
    }
    _buff_ptr_out = 0;
    if (close) 
    {
        _state = CLOSED;
        return S5_CONNECTION_CLOSED;
    }
    cevents &= ~S5E_WRNEED;
    return S5_SUCCESS;
}


SOCKS_Returns SOCKS_Client::setup_communication()
{
    _cpipe = new Pipe();
    _hpipe = new Pipe();
    if (!_cpipe->valid() || !_hpipe->valid())
    {
        SOCKS_Returns ret;
        if (_cpipe->fatal_err || _hpipe->fatal_err){
            _error = TCP_SERVER_ERROR;
            cevents |= S5E_WRNEED;
        }
        delete _cpipe;
        _cpipe = nullptr;
        delete _hpipe;
        _hpipe = nullptr;
        return S5_WRAGAIN;
    }
    return S5_SUCCESS;
}


SOCKS_Client::SOCKS_Client(int fd, int flags, sockaddr addr, socklen_t addrlen) : _state (INITIAL), _repeat_state(NONE), _buff_ptr_in (0), _buff_ptr_out (0), _network_addr(addr), _addrlen(addrlen), _fd(fd), _cpipe(nullptr), _hpipe(nullptr), _error(0), _connected_to_target(false), target_host_fd(nullptr), timer(nullptr), cevents(0), hevents(0)
{
    memset(_buffer, 0, sizeof(_buffer));
    if (_fd.add_status_flags(flags) == -1)
        cevents |= S5E_FATAL;
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

void SOCKS_Client::register_events(const uint32_t events, bool client)
{
    if (client)
    {
        if (events & EPOLLIN) cevents |= S5E_IN;
        if (events & EPOLLOUT) cevents |= S5E_OUT;
        if (events & EPOLLRDHUP) cevents |= S5E_RDHUP;
        if (events & EPOLLERR) cevents |= S5E_WRHUP;
        if (events & EPOLLHUP) cevents |= (S5E_RDHUP | S5E_WRHUP);
    }
    else
    {
        if (events & EPOLLIN) hevents |= S5E_IN;
        if (events & EPOLLOUT) hevents |= S5E_OUT;
        if (events & EPOLLRDHUP) hevents |= S5E_RDHUP;
        if (events & EPOLLERR) hevents |= S5E_WRHUP;
        if (events & EPOLLHUP) hevents |= (S5E_RDHUP | S5E_WRHUP);
    }
}

const SOCKS_Returns SOCKS_Client::handle_request() {
    if (_state == CLOSED)
        return S5_CONNECTION_CLOSED;
    if (!c_read_avail || !c_write_avail) return S5_BAD_REQUEST;

    if (_error != 0)
    {
        uint8_t init_buff[4] = { 0x05, _error,  0x00, 0x01};
        SOCKS_WRITE(init_buff, 4, 10, true);
    }
    // What would happen when the state machine doens't need EPOLLIN right now rather EPOLLOUT but EPOLLIN is available, then how to delegate it or vice versa

    if (_state == INITIAL)
    {
        if (_buff_ptr_in < 2)
            SOCKS_READ(0, 2);
        if (_buff_ptr_in == 2) 
        {
            if (_buffer[0] != 0x05)
            {
                std::cerr << "SOCKS_Client: INITIAL: Unsupported version" << std::endl;
                uint8_t init_buff[2] = { 0x05, 0xFF };
                SOCKS_WRITE(init_buff, 2, 2, true);
            }
        }
        if (_buff_ptr_in > 2 && _buff_ptr_in < 2 + _buffer[1])
            SOCKS_READ(2, _buffer[1]);
    }
    if (_buff_ptr_in == 2 + _buffer[1])
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
            SOCKS_WRITE(init_buff, 2, 2, true);
        }

        SOCKS_WRITE(nullptr, 0, 2, false);
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
            SOCKS_READ(0, 4);
        if (_buff_ptr_in == 4) {
            if (_buffer[0] != 0x05)
            {
                std::cerr << "SOCKS_Client: REQUESTS: Unsupported version" << std::endl;
                uint8_t init_buff[4] = { 0x05, 0x02, 0x00, 0x01 }; // connection not allowed by ruleset
                SOCKS_WRITE(init_buff, 4, 10, true);
            }
            if (_buffer[1] != 0x01 && _buffer[1] != 0x02 && _buffer[1] != 0x03)
            {
                std::cerr << "SOCKS_Client: REQUESTS: Unknown command" << std::endl;
                uint8_t init_buff[4] = { 0x05, 0x07, 0x00, 0x01 }; // Command not supported
                SOCKS_WRITE(init_buff, 4, 10, true);
            }
            if (_buffer[2] != 0x00)
            {
                std::cerr << "SOCKS_Client: REQUESTS: Reserved byte used" << std::endl;
                uint8_t init_buff[4] = { 0x05, 0x02, 0x00, 0x01 }; // connection not allowed by ruleset
                SOCKS_WRITE(init_buff, 4, 10, true);
            }
            if (_buffer[3] != 0x01 || _buffer[3] != 0x03 || _buffer[3] != 0x04)
            {
                std::cerr << "SOCKS_Client: REQUESTS: Unknown Address type" << std::endl;
                uint8_t init_buff[4] = { 0x05, 0x08, 0x00, 0x01 }; // Address type not supported
                SOCKS_WRITE(init_buff, 4, 10, true);
            }
        }
        if (_buff_ptr_in >= 4)
        {
            if (_buffer[3] == 0x01)
                SOCKS_READ(4, 6);
            else if (_buffer[3] == 0x03)
            {
                if (_buff_ptr_in == 4)
                    SOCKS_READ(4, 1);
                if (_buff_ptr_in >= 5)
                    SOCKS_READ(5, _buffer[4] + 2);
            }
            else 
                SOCKS_READ(4, 18);
            _state = EVALUATION_AND_REPLY;
        }
    }

    if (_state == EVALUATION_AND_REPLY)
    {
        // Currently only supports CONNECT
        if (_repeat_state != _state)
            _buffer[1] = (uint8_t)open_new_connection_to_target(*this);

        uint8_t init_buff[4] = { 0x05, _buffer[1], 0x00, 0x01 };

        switch (_buffer[1]) {
            case TCP_INPROGRESS:
                return S5_ADD_TARGET_CONNECT_TIMER; // Shoudn't return a second time (maybe remove EPOLL on clientfd??)
            case TCP_SUCCESS:
            {
                SOCKS_Returns ret;
                if ((ret = setup_communication()) == S5_SUCCESS);
                else return ret;
                SOCKS_WRITE(nullptr, 0, _buffer[3] == 0x01 ? 4 : 16, false);
                _connected_to_target = true;
                _state = COMMUNICATION;
                return S5_SUCCESS;
            }
            case TCP_EAGAIN:
                return S5_EAGAIN;
            default:
                SOCKS_WRITE(init_buff, 4, 10, true);
        }
    }
    return S5_BAD_REQUEST;
}

const SOCKS_Returns SOCKS_Client::handle_communication(const bool with_client, const bool read)
{
    if (_state != COMMUNICATION) return S5_BAD_REQUEST;

    int err;
    File_Descriptor& fd = (with_client) ? _fd : *target_host_fd;
    Pipe& in = (with_client) ? *_cpipe : *_hpipe;
    Pipe& out = (with_client) ? *_hpipe : *_cpipe;
    uint8_t& ev = (with_client) ? cevents : hevents;
    uint8_t& comp_ev = (with_client) ? hevents : cevents;

    if (ev & S5E_CLOSED) return S5_CONNECTION_CLOSED;
    if (read){
        if (ev & S5E_IN)
        {
            err = in.nonblocking_read_from(fd);
            if (in.get_last_read() && !(comp_ev & S5E_WRHUP))
                comp_ev |= S5E_WRNEED;

            if (err != 0)
            {
                switch (err) {
                    case EAGAIN:
                        ev &= (~S5E_IN & ~S5E_RDNEED);
                        return S5_SUCCESS;
                    case EBADF:
                    case EINVAL:
                    case ESPIPE:
                    default:
                        std::cout << strerror(err) << std::endl;
                        ev &= S5E_CLOSED;
                        return S5_CONNECTION_CLOSED;
                    case ENOMEM:
                        ev |= S5E_RDNEED;
                        return S5_RDAGAIN;
                }
            }
            else
            {
                ev &= ~S5E_IN & ~S5E_RDNEED;
                ev |= S5E_RDHUP;
                if ((ev & S5E_WRHUP))
                {
                    ev &= S5E_CLOSED;
                    return S5_CONNECTION_CLOSED;
                }

                ev |= S5E_WRNEED;
                return S5_WRAGAIN;
            }
        }
        else
            return S5_BAD_REQUEST;
    }
    else
    {
        if (ev & S5E_OUT)
        {
            if (ev & S5E_WRHUP)
            {
                ev &= (~S5E_WRNEED & ~S5E_OUT);
                if ((ev & S5E_RDHUP))
                {
                    ev &= S5E_CLOSED;
                    return S5_CONNECTION_CLOSED;
                }
                ev |= S5E_RDNEED;
                return S5_RDAGAIN;
            }
            int flag = ((ev & S5E_RDHUP) && !(ev & S5E_RDNEED)) ? 0 : SPLICE_F_MORE;
            err = out.nonblocking_write_to(fd, flag);

            if (err != 0)
            {
                switch (err) {
                    case EAGAIN:
                        ev |= S5E_WRNEED;
                        ev &= ~S5E_OUT;
                        break; // same returning value as of case when err=0 since that represents there is nothing in the pipe to write
                    case EBADF:
                    case EINVAL:
                    case ESPIPE:
                    default:
                        std::cout << strerror(err) << std::endl;
                        ev &= S5E_CLOSED;
                        return S5_CONNECTION_CLOSED;
                    case ENOMEM:
                        std::cout << strerror(err) << std::endl;
                        ev |= S5E_WRNEED;
                        return S5_WRAGAIN;
                }
            }
            else 
                ev &= ~S5E_WRNEED;
            return S5_SUCCESS;
        }
        else
            return S5_BAD_REQUEST;
    }
}

const SOCKS_Returns SOCKS_Client::check_target_connection(const bool using_var)
{
    if (_state != EVALUATION_AND_REPLY && !using_var)
    {
        _error = TCP_SERVER_ERROR;
        cevents |= S5E_WRNEED;
        return S5_WRAGAIN;
    }
    if (using_var){
        if (_connected_to_target) return S5_SUCCESS;
        _error = TCP_HOST_UNREACHABLE;
        cevents |= S5E_WRNEED;
        return S5_WRAGAIN;
    }
    else{
        if ((_error = check_inprogress_connection(target_host_fd)) != TCP_SUCCESS)
        {
            cevents |= S5E_WRNEED;
            return S5_WRAGAIN;
        }
        _state = COMMUNICATION;
        _connected_to_target = true;
        _buffer[1] = TCP_SUCCESS;
        return S5_SUCCESS;
    }
}


SOCKS_Client::~SOCKS_Client()
{
    if (_cpipe != nullptr)
        delete _cpipe;
    if (_hpipe != nullptr)
        delete _hpipe;
    if (target_host_fd != nullptr)
        delete target_host_fd;
    if (timer != nullptr)
        delete timer;
}