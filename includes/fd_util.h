#ifndef FD_UTIL_H
#define FD_UTIL_H

#include <cstdint>
#include <sys/types.h>


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
    int nonblocking_read(void* buffer, size_t nbytes, uint32_t& read_avail);
    int nonblocking_write(const void* buffer, size_t count, uint32_t& write_avail);

    int handle_error() const;

    ~File_Descriptor();
};

class Timer_File_Descriptor
{
private:
    int _timer_fd;
    File_Descriptor* associated_fd;
};





#endif