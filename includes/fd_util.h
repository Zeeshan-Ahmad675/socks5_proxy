#ifndef FD_UTIL_H
#define FD_UTIL_H

#include <sys/timerfd.h> // timerfd_create()
#include <cstddef> // size_t
#include <sys/types.h> // ssize_t


class SOCKS_Client;

class File_Descriptor 
{
private:
    int _fd;
    ssize_t _bytes_last_read;
    ssize_t _bytes_last_write;
    friend class Pipe;
public:
    explicit File_Descriptor();
    explicit File_Descriptor(int fd);
    File_Descriptor(File_Descriptor& other) = delete;
    File_Descriptor(File_Descriptor&& other) noexcept;
    File_Descriptor& operator=(File_Descriptor&& other) noexcept;
    
    int get_fd() const { return _fd; };
    ssize_t get_last_read() const { return _bytes_last_read; }
    ssize_t get_last_write() const { return _bytes_last_write; }
    bool operator==(const File_Descriptor& other) const;
    bool operator==(int other) const;
    bool operator!=(const File_Descriptor& other) const;
    bool operator!=(int other) const;

    int get_status_flags() const;
    int add_status_flags(int flags) const;
    int nonblocking_read(void* buffer, size_t nbytes);
    int nonblocking_write(const void* buffer, size_t count);

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

class Pipe
{
private:
    File_Descriptor _readfd;
    File_Descriptor _writefd;
    
public:
    bool fatal_err;

    explicit Pipe();
    bool valid() const
    {
        return (_readfd != -1) && (_writefd != -1);
    }

    ssize_t get_last_read() const { return _writefd._bytes_last_read; }
    ssize_t get_last_write() const { return _readfd._bytes_last_write; }
    int nonblocking_read_from(File_Descriptor& fd);
    int nonblocking_write_to(File_Descriptor& fd, const unsigned int flags);
};





#endif