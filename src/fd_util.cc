#include "fd_util.h"
#include <fcntl.h>  // fcntl()
#include <sys/timerfd.h>
#include <unistd.h> // read()
#include <sys/socket.h> // accept()
#include <errno.h>  // errno
#include <ctime>
#include <cstring> // strerror()
#include <cstdint> //uint8_t
#include <iostream> // perror(), cerr

File_Descriptor::File_Descriptor()
: _fd (-1) {}

File_Descriptor::File_Descriptor(int fd)
: _fd (fd) {}


bool File_Descriptor::operator==(const File_Descriptor& other) const
{
    return _fd == other.get_fd();
}

bool File_Descriptor::operator==(int other) const
{
    return _fd == other;
}

int File_Descriptor::get_status_flags() const 
{
    int flags;
    if ((flags = fcntl(_fd, F_GETFL)) == -1) {
        perror("File_Descriptor: get_status_flags: fcntl GETFL");
        return -1;
    }
    return flags;
}

int File_Descriptor::add_status_flags(int flags) const
{
    int f = get_status_flags();
    if (f == -1) return -1;

    if (fcntl(_fd, F_SETFL, f | flags) == -1)
    {
        perror("File_Descriptor: add_status_flags: fcntl SETFL");
        return -1;
    }
    else
        return 0;
}

int File_Descriptor::nonblocking_read(void* buffer, size_t nbytes, bool& read_avail)
{
    if (!read_avail || nbytes <= 0) return 0;

    ssize_t n = 0, i = 0;
    do {
        i = read(_fd, (uint8_t*)buffer + n, nbytes - n);
        n += i;
    } while (i != -1 && n != nbytes);
    
    if (i == -1){
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            read_avail = 0;
            return n + 1;
        }
        else
        {
            perror("File_Descriptor: nonblocking_read: read");
            return 0;
        }
    }
    else
        return n;
}

int File_Descriptor::nonblocking_write(const void* buffer, size_t count, bool& write_avail) 
{
    if (!write_avail || count <= 0) return 0;
    
    ssize_t n = 0, i = 0;
    do {
        i = write(_fd, (uint8_t*)buffer + n, count - n);
        n += i;
    } while (i != -1 && n != count);

    if (i == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            write_avail = 0;
            return n + 1;
        }
        else
        {
            perror("File_Descriptor: nonblocking_write: write");
            return 0;
        }
    }
    else
        return n;
}


File_Descriptor::~File_Descriptor()
{
    if (_fd != -1)
        close(_fd);
}




Timer_File_Descriptor::Timer_File_Descriptor(int clockid, int flags)
    : File_Descriptor(timerfd_create(clockid, flags)) {}


int Timer_File_Descriptor::settime(int flags, const struct itimerspec* new_value, struct itimerspec* old_value) const
{
    return timerfd_settime(this->get_fd(), flags, new_value, old_value);
}