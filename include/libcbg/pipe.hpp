#pragma once

#include <vector>
#include <cstddef>

namespace cbg
{
    class Pipe
    {
    public:
        explicit Pipe(bool close_on_exec);
        ~Pipe();

        int get_read() const { return fds_[READ_FD]; }
        int get_write() const { return fds_[WRITE_FD]; }
        int release_read();
        int release_write();
        void close_read();
        void close_write();

        std::vector<std::byte> read();
        void write(std::byte *from, std::size_t bytes);

    private:
        static constexpr unsigned READ_FD = 0;
        static constexpr unsigned WRITE_FD = 1;
        int fds_[2];
    };
} // namespace cbg
