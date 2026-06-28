#include "fd_util.h"
#include "tcp.h"
#include "epoll_util.h"
#include "socks.h"
#include <cstdlib> // exit()
#include <sys/epoll.h>
#include <iostream> // perror()

#define SOCKS5_PORT "1080"
#define MAX_QUEUED 100
#define MAX_EVENTS 10

// void shutdown_socks_client(const SOCKS_Client& client)

int main()
{
    int nfds;
    File_Descriptor listener;
    Epoll_Instance epoll_handle(MAX_EVENTS);

    listener = start_tcp_server(SOCKS5_PORT, MAX_QUEUED);
    if (listener == -1)
    {
        perror("tcpserv");
        exit(EXIT_FAILURE);
    }

    epoll_handle.add_to_interest(listener, FILE_DESCRIPTOR_REF, &listener, EPOLLIN | EPOLLET);

    for (;;) 
    {
        nfds = epoll_handle.get_events(-1);

        for (int i = 0; i < nfds; i++) 
        {
            // Check for returned epoll events flags missing
            struct epoll_event_data ev_data = *(struct epoll_event_data*)(epoll_handle.events_buff[i].data.ptr);
            uint32_t rcv_events = epoll_handle.events_buff[i].events;

            if (ev_data.type == FILE_DESCRIPTOR_REF)
            {
                if (rcv_events & EPOLLIN)
                {
                    File_Descriptor *triggered = (File_Descriptor*)ev_data.ptr;
                    if (*triggered == listener) 
                        epoll_handle.nonblocking_socks_client_accept(*triggered);
                }
                if (rcv_events & EPOLLPRI)
                {
                    // handle out of band data (no need since TCP RFC 9293 suggest against using urgent mechanism and OOB data is urgent data in TCP)
                    // ioctl thing also need not be handled since it makes the application non-portable
                    // cgroups.events: file which handles the resources in the cgroupfs (is it needed to be handled ??) 
                }
            }
            else if (ev_data.type == SOCKS_CLIENT_REF)
            {
                SOCKS_Client *client = (SOCKS_Client*)ev_data.ptr;
                
                if (rcv_events & EPOLLRDHUP)
                    client->err |= S5E_RDHUP;
                if (rcv_events & EPOLLERR)
                    client->err |= S5E_WRHUP;
                
                // Check for errors on read and write functions in socks.cc
                
                if (rcv_events & (EPOLLIN | EPOLLOUT))
                {
                    // is timer nullptr or what ? do we need to check it ?
                    SOCKS_Returns ret = client->handle_request(rcv_events);

                    if (ret == S5_REMOVE_EPOLLIN)
                    {
                        if (rcv_events & EPOLLIN)
                            epoll_handle.modify_event(client->get_file_descriptor(), SOCKS_CLIENT_REF, client, (rcv_events & (~EPOLLIN)) | EPOLLET);
                    }
                    else if (ret == S5_ADD_TARGET_TO_EPOLL)
                        epoll_handle.add_to_interest(client->target_host_fd, TARGET_HOST_REF, client, EPOLLIN | EPOLLOUT | EPOLLET);
                    else if (ret == S5_ADD_TARGET_CONNECT_TIMER)
                    {
                        epoll_handle.add_to_interest(client->target_host_fd, TARGET_CONNECTION_REF, client, EPOLLOUT | EPOLLET);
                        Timer_File_Descriptor* tfd = new Timer_File_Descriptor(CLOCK_MONOTONIC, TFD_NONBLOCK);

                        // need to handle the error of timerfd_create()

                        struct itimerspec new_value = { { 0, 0 }, { 120, 0 } }; // 2 min single expiry timer
                        tfd->settime(0, &new_value, nullptr);
                        epoll_handle.add_to_interest(*tfd, TARGET_TIMER_REF, client, EPOLLIN | EPOLLET);
                        client->timer = tfd;
                    }
                    else if (ret == S5_CONNECTION_CLOSED) 
                    {
                        epoll_handle.remove_from_interest(client->get_file_descriptor());
                        if (client->target_host_fd != -1)
                            epoll_handle.remove_from_interest(client->target_host_fd);
                        if (client->timer != nullptr)
                            epoll_handle.remove_from_interest(*client->timer);
                        delete client;
                    }
                }
            }
            else if (ev_data.type == TARGET_CONNECTION_REF)
            {
                SOCKS_Client *associated_client = (SOCKS_Client*)ev_data.ptr;
                if (associated_client->timer == nullptr) continue;
                
                SOCKS_Returns ret = associated_client->check_target_connection(false);
                if (ret == S5_BAD_REQUEST) continue;
                if (ret == S5_SUCCESS)
                    epoll_handle.modify_event(associated_client->target_host_fd, TARGET_HOST_REF, associated_client, rcv_events | EPOLLIN | EPOLLET);
                else
                    epoll_handle.remove_from_interest(associated_client->target_host_fd);
                epoll_handle.remove_from_interest(*associated_client->timer);
            }
            else if (ev_data.type == TARGET_TIMER_REF)
            {
                SOCKS_Client *associated_client = (SOCKS_Client*)ev_data.ptr;
                SOCKS_Returns ret = associated_client->check_target_connection(true);
                if (ret != S5_SUCCESS)
                    epoll_handle.remove_from_interest(associated_client->target_host_fd);

                epoll_handle.remove_from_interest(*associated_client->timer);
                delete associated_client->timer;
                associated_client->timer = nullptr;
            }
        }
    }
}