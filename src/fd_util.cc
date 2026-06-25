#include "fd_util.h"
#include <fcntl.h>  // fcntl()
#include <unistd.h> // read()
#include <sys/socket.h> // accept()
#include <errno.h>  // errno
#include <iostream> // perror(), cerr
#include <cstring> // strerror()

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
        switch (errno) {
            case EBADF:
                std::cerr << "Error: " << _fd << " is not an open file descriptor - EBADF" << std::endl;
                perror("fcntl F_GETFL");
                break;
            case EACCES:
                std::cerr << "Error: Operation is prohibited by locks held by other processes - EACCES" << std::endl;
                perror("fcntl F_GETFL");
                break;
            case EAGAIN:
                std::cerr << "Error: The operation is prohibited because the file has been memory-mapped by another process - EAGAIN" << std::endl;
                perror("fcntl F_GETFL");
                break;
            case EINVAL:
                std::cerr << "The value F_GETFL is not recognized by this kernel - EINVAL" << std::endl;
                perror("fcntl F_GETFL");
                break;
            default:
                std::cerr << "Error: Unknown error (errno=" << errno << ") - " << strerror(errno) << std::endl;
                perror("fcntl F_GETFL");
                break;
        }
        return -1;
    }
    return flags;
}

int File_Descriptor::add_status_flags(int flags) const
{
    int f = get_status_flags();

    if (fcntl(_fd, F_SETFL, f | flags) == -1)
        handle_error();
    else
        return 0;
}

int File_Descriptor::nonblocking_read(void* buffer, size_t nbytes, uint32_t& read_avail)
{
    ssize_t n = 0, i;
    do {
        i = read(_fd, (char*)buffer + n, nbytes - n);
        n += i;
    } while (i != -1 && n != nbytes);
    
    if (i == -1){
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            read_avail = 0;
            return n + 1;
        }
        else
            handle_error();
    }
    else
        return n;
}

int File_Descriptor::nonblocking_write(const void* buffer, size_t count, uint32_t& write_avail) 
{
    ssize_t n = 0, i;
    do {
        i = write(_fd, (char*)buffer + n, count - n);
        n += i;
    } while (i != -1 && n != count);

    if (i == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            write_avail = 0;
            return n + 1;
        }
        else
            handle_error();
    }
    else
        return n;
}

int File_Descriptor::handle_error() const
{
    std::cerr << "Error on file descriptor (" << _fd << "): " << strerror(errno) << std::endl;
    perror("File_Descriptor operation failed");
    return -1;
}


File_Descriptor::~File_Descriptor()
{
    if (_fd != -1)
        close(_fd);
}