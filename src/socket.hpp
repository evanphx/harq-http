#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <string>

#include "write_set.hpp"

namespace wire {
  class Message;
}

class Socket {
  WriteSet writes_;

public:
  int fd;

  Socket(int fd)
    : fd(fd)
  {}

  void set_nonblock();

  WriteStatus write(const wire::Message& msg);
  WriteStatus write(const std::string val);
  WriteStatus write_with_size(const std::string val);

  WriteStatus flush() {
    return writes_.flush(fd);
  }
};

#endif
