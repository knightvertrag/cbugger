#pragma once

#include <stdexcept>
#include <cstring>

namespace cbg {

    class Error : public std::runtime_error {
        private:
            Error(const std::string &what)
                : std::runtime_error(what) {}
        public:
            [[noreturn]]
            static void send(const std::string &what) {throw Error(what);}
            [[noreturn]]
            static void send_errno(const std::string &prefix) {
                std::string error_message = prefix + ": " + std::strerror(errno);
                throw Error(error_message);
            }
    };
}