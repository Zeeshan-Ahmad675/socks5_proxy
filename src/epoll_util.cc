#include "epoll_util.h" 
#include <systemd/sd-daemon.h>  // sd_is_socket()
#include <cstdlib> // exit()
#include <fcntl.h> // O_NONBLOCK
#include <iostream> // perror()


Epoll_Instance::Epoll_Instance(int max_events)
{
    // Check if some flags are returned when we use epoll in the manual

    if (max_events > 0){ 
        _max_events = max_events;
        events_buff = new struct epoll_event[_max_events];
    }
    else
        throw std::runtime_error("Epoll_Instance: max_events must be positive and nonzero");

    
    if ((_epollfd = File_Descriptor(epoll_create1(0))) == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }
}

int Epoll_Instance::add_to_interest(const File_Descriptor& fd, epoll_event_data_type ev_dtype, void* ev_daddr, uint32_t events_and_flags)
{
    // Stack allocation, so should be an error due to this
    struct epoll_event_data ep_edata;
    ep_edata.type = ev_dtype;
    ep_edata.ptr = ev_daddr;

    _ev.events = events_and_flags;
    _ev.data.ptr = &ep_edata;

    if (epoll_ctl(_epollfd.get_fd(), EPOLL_CTL_ADD, fd.get_fd(), &_ev) == -1)
        handle_error();
    else 
        return 0;
}

int Epoll_Instance::modify_event(const File_Descriptor& fd, epoll_event_data_type ev_dtype, void* ev_daddr, uint32_t events_and_flags)
{
    // Stack allocation, so should be an error due to this
    struct epoll_event_data ep_edata;
    ep_edata.type = ev_dtype;
    ep_edata.ptr = ev_daddr;

    _ev.events = events_and_flags;
    _ev.data.ptr = &ep_edata;

    if (epoll_ctl(_epollfd.get_fd(), EPOLL_CTL_MOD, fd.get_fd(), &_ev) == -1)
        handle_error();
    else 
        return 0;
}

int Epoll_Instance::remove_from_interest(const File_Descriptor& fd) const
{
    if (epoll_ctl(_epollfd.get_fd(), EPOLL_CTL_DEL, fd.get_fd(), NULL) == -1)   // NULL here requires Linux 2.6.9 or higher
        handle_error();
    else
        return 0;
}



int Epoll_Instance::get_events(int timeout)
{
    int nfds;
    if ((nfds = epoll_wait(_epollfd.get_fd(), events_buff, _max_events, timeout)) == -1)
        handle_error();
    else 
        return nfds;
}


int Epoll_Instance::nonblocking_socks_client_accept(File_Descriptor& listener)
{
    int listener_fd = listener.get_fd();
    if (!sd_is_socket(listener_fd, 0, 0, -1)) return -1;

    int n = 0, i;
    sockaddr addr;
    socklen_t addrlen;
    // Check for other error conditions so that the loop doesn't constinue indefinitely
    while (true) {
        i = accept(listener_fd, &addr, &addrlen);
        if (i == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)) break;
        
        SOCKS_Client* client = new SOCKS_Client(i, O_NONBLOCK, addr, addrlen);
        if (add_to_interest(client->get_file_descriptor(), SOCKS_CLIENT_REF, client, EPOLLIN | EPOLLOUT | EPOLLET) != 0) {
            std::cerr << "Unable to add client to epoll interest list. Closing connection" << std::endl;
            delete client;
        }
    }
    return 0;
}


Epoll_Instance::~Epoll_Instance()
{
    delete[] events_buff;
}