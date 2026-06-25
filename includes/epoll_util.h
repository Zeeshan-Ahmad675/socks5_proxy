#ifndef EPOLL_UTIL_H
#define EPOLL_UTIL_H

#include "fd_util.h"
#include "socks.h"
#include <sys/epoll.h>  // epoll_event
#include <sys/types.h>  // uint32_t


enum epoll_event_data_type
{
    FILE_DESCRIPTOR_REF,
    SOCKS_CLIENT_REF
};

struct epoll_event_data
{
    epoll_event_data_type type;
    void* ptr;
};

class Epoll_Instance {
private:
    File_Descriptor _epollfd;
    int _max_events;
    struct epoll_event _ev;
public:
    struct epoll_event* events_buff;

    Epoll_Instance(int max_events);
    int add_to_interest(const File_Descriptor& fd, epoll_event_data_type ev_dtype, void* ev_daddr, uint32_t events_and_flags);
    int modify_event(const File_Descriptor& fd, epoll_event_data_type ev_dtype, void* ev_daddr, uint32_t events_and_flags);
    int remove_from_interest(const File_Descriptor& fd) const;

    int add_to_interest(const SOCKS_Client& client, epoll_event_data_type ev_dtype, void* ev_daddr, uint32_t events_and_flags)
    {
        return add_to_interest(client.get_file_descriptor(), ev_dtype, ev_daddr, events_and_flags);
    }
    int modify_event(const SOCKS_Client& client, epoll_event_data_type ev_dtype, void* ev_daddr, uint32_t events_and_flags)
    {
        return modify_event(client.get_file_descriptor(), ev_dtype, ev_daddr, events_and_flags);
    }
    int remove_from_interest(const SOCKS_Client& client) const
    {
        return remove_from_interest(client.get_file_descriptor());
    }

    int get_events(int timeout);
    int nonblocking_socks_client_accept(File_Descriptor& listener);

    int handle_error() const;

    ~Epoll_Instance();
};



#endif