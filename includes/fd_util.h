#ifndef FD_UTIL_H
#define FD_UTIL_H

#include <sys/timerfd.h> // timerfd_create()
#include <cstddef> // size_t


class SOCKS_Client;

class File_Descriptor 
{
private:
    int _fd;
public:
    explicit File_Descriptor();
    explicit File_Descriptor(int fd);
    File_Descriptor(File_Descriptor& other) = delete;
    
    int get_fd() const { return _fd; };
    bool operator==(const File_Descriptor& other) const;
    bool operator==(int other) const;

    int get_status_flags() const;
    int add_status_flags(int flags) const;
    int nonblocking_read(void* buffer, size_t nbytes, bool& read_avail);
    int nonblocking_write(const void* buffer, size_t count, bool& write_avail);

    ~File_Descriptor();
};


class Timer_File_Descriptor: public File_Descriptor
{
public:
    Timer_File_Descriptor() = delete;
    explicit Timer_File_Descriptor(int clockid, int flags);

    int settime(int flags, const struct itimerspec* new_value, struct itimerspec* old_value) const;

    ~Timer_File_Descriptor() {}
};





#endif