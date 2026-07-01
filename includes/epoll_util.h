#ifndef EPOLL_UTIL_H
#define EPOLL_UTIL_H

#include "fd_util.h"
#include <sys/epoll.h>  // epoll_event
#include <cstdint> // uint8_t, uint32_t
#include <unordered_map>

enum epoll_event_data_type: uint8_t
{
    FILE_DESCRIPTOR_REF,
    SOCKS_CLIENT_REF,
    TARGET_TIMER_REF,
    TARGET_CONNECTION_REF,
    TARGET_HOST_REF,
    CLIENT_REF,
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
    std::unordered_map<epoll_event_data*, bool> active_map;

    Epoll_Instance(int max_events);
    int add_to_interest(const File_Descriptor& fd, epoll_event_data_type ev_dtype, void* ev_daddr, uint32_t events_and_flags);
    int modify_event(const File_Descriptor& fd, epoll_event_data* ep_edata, uint32_t events_and_flags);
    int remove_from_interest(const File_Descriptor& fd) const;

    int get_events(int timeout);
    int nonblocking_socks_client_accept(File_Descriptor& listener);

    int handle_error() const;

    ~Epoll_Instance();
};



#endif