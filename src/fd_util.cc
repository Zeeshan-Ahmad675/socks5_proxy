#undef _GNU_SOURCE
#define _GNU_SOURCE
// #define _FILE_OFFSET_BITS 64 // Needed??

#include "fd_util.h"
#include <fcntl.h>  // O_* flags, fcntl(), splice()
#include <sys/timerfd.h>
#include <unistd.h> // read(), pipe()
#include <errno.h>  // errno
#include <cstring> //  strerror()
#include <cstdint> // SIZE_MAX, uint8_t
#include <sys/types.h> // ssize_t
#include <climits> // SSIZE_MAX
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

int File_Descriptor::nonblocking_read(void* buffer, size_t nbytes)
{
    if (nbytes <= 0) return 0;

    _bytes_last_read = 0;
    ssize_t i = 0;
    while(i != -1 && i != 0 && _bytes_last_read != nbytes) 
    {
        _bytes_last_read += i;
        i = read(_fd, (uint8_t*)buffer + _bytes_last_write, nbytes - _bytes_last_read);
    } 
    
    if (i == -1)
        return errno;
    return 0;
}

int File_Descriptor::nonblocking_write(const void* buffer, size_t count) 
{
    if (count <= 0) return 0;
    
    _bytes_last_write = 0;
    ssize_t i = 0;
    while (i != -1 && _bytes_last_write != count)
    {
        _bytes_last_write += i;
        i = write(_fd, (uint8_t*)buffer + _bytes_last_write, count - _bytes_last_write);
    }

    if (i == -1) 
        return errno;
    return 0;
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





Pipe::Pipe() : fatal_err(false)
{
    int pipefd[2];
    if (pipe2(pipefd, O_NONBLOCK) == -1)
    {
        switch(errno) {
            case EFAULT:
            case EINVAL:
                fatal_err = true;
        }
        perror("Pipe: Pipe(): pipe2");
        _readfd = File_Descriptor();
        _writefd = File_Descriptor();
    }
    else
    {
        _readfd = File_Descriptor(pipefd[0]);
        _writefd = File_Descriptor(pipefd[1]);
    }
}

int Pipe::nonblocking_read_from(File_Descriptor& fd)
{
    fd._bytes_last_write = 0;
    ssize_t i = 0;
    while(i != -1 || i != 0)
    {
        fd._bytes_last_write += i;
        i = splice(fd.get_fd(), NULL, _writefd.get_fd(), NULL, SIZE_MAX / 2, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    }
    _writefd._bytes_last_read = fd._bytes_last_write;
    if (i == -1)
        return errno;
    return 0;
}

int Pipe::nonblocking_write_to(File_Descriptor& fd, const unsigned int flags)
{
    fd._bytes_last_read = 0;
    ssize_t i = 0;
    while(i != -1 && i != 0)
    {
        fd._bytes_last_read += i;
        i = splice(_readfd.get_fd(), NULL, fd.get_fd(), NULL, SIZE_MAX / 2, flags | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    }
    _readfd._bytes_last_write = fd._bytes_last_read;
    if (i == -1)
        return errno;
    return 0;
}