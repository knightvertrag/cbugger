#include <unistd.h>
#include <fcntl.h>
#include <utility>
#include <libcbg/error.hpp>
#include <libcbg/pipe.hpp>

cbg::Pipe::Pipe(bool close_on_exec)
{
    if (pipe2(fds_, close_on_exec ? O_CLOEXEC : 0) < 0)
    {
        Error::send_errno("Pipe creation failed");
    }
}

cbg::Pipe::~Pipe() 
{
    close_read();
    close_write();
}

int cbg::Pipe::release_read()
{
    return std::exchange(fds_[READ_FD], -1);
}

int cbg::Pipe::release_write()
{
    return std::exchange(fds_[WRITE_FD], -1);
}

void cbg::Pipe::close_read()
{
    if (fds_[READ_FD] != -1)
    {
        close(fds_[READ_FD]);
        fds_[READ_FD] != -1;
    }
}

void cbg::Pipe::close_write()
{
    if (fds_[WRITE_FD] != -1)
    {
        close(fds_[WRITE_FD]);
        fds_[WRITE_FD] = -1;
    }
}

std::vector<std::byte> cbg::Pipe::read()
{
    char buf[1024];
    int chars_read;
    if ((chars_read = ::read(fds_[READ_FD], buf, sizeof(buf))) < 0)
    {
        Error::send_errno("Could not read from pipe");
    }

    auto bytes = reinterpret_cast<std::byte*>(buf);
    return std::vector<std::byte>(bytes, bytes + chars_read);
}

void cbg::Pipe::write(std::byte* from, std::size_t bytes)
{
    if (::write(fds_[WRITE_FD], from, bytes) < 0)
    {
        Error::send_errno("Could not write to pipe");
    }
}