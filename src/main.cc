#include "fd_util.h"
#include "tcp.h"
#include "epoll_util.h"
#include "socks.h"
#include <cstdlib> // exit()
#include <iostream> // perror()

#define SOCKS5_PORT "1080"
#define MAX_QUEUED 100
#define MAX_EVENTS 10



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

            if (ev_data.type == FILE_DESCRIPTOR_REF)
            {
                File_Descriptor *triggered = (File_Descriptor*)ev_data.ptr;
                if (*triggered == listener) 
                    epoll_handle.nonblocking_socks_client_accept(*triggered);
            }
            else
            {
                SOCKS_Client *requesting_client = (SOCKS_Client*)ev_data.ptr;
                SOCKS_Returns ret = requesting_client->handle_request(epoll_handle.events_buff[i].events);

                if (ret == S5_REMOVE_EPOLLIN)
                {
                    epoll_handle.modify_event(*requesting_client, SOCKS_CLIENT_REF, requesting_client, epoll_handle.events_buff[i].events & !EPOLLIN);
                }
                else if (ret == S5_CONNECTION_CLOSED) 
                {
                    epoll_handle.remove_from_interest(*requesting_client);
                    delete requesting_client;
                }
            }
        }
    }
}