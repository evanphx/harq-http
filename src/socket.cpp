#include "harq.hpp"
#include "socket.hpp"
#include "debugs.hpp"

#include "wire.pb.h"

#include <iostream>

#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

WriteStatus Socket::write(const std::string& val) {
  writes_.add(val);

  WriteStatus stat = writes_.flush(fd);

  switch(stat) {
  case eOk:
    debugs << "Writes flushed successfully\n";
    break;
  case eWouldBlock:
    debugs << "Writes would have blocked, NOT fully flushed\n";
    break;
  case eFailure:
    debugs << "Writes failed, socket busted\n";
    break;
  }

  return stat;
}

WriteStatus Socket::write_with_size(const std::string& val) {
  union sz {
    char buf[4];
    uint32_t i;
  } sz;

  sz.i = htonl(val.size());

  debugs << "Queue'd data of size " << val.size() << " bytes\n";

  writes_.add(std::string(sz.buf,4));
  writes_.add(val);

  WriteStatus stat = writes_.flush(fd);

  switch(stat) {
  case eOk:
    debugs << "Writes flushed successfully\n";
    break;
  case eWouldBlock:
    debugs << "Writes would have blocked, NOT fully flushed\n";
    break;
  case eFailure:
    debugs << "Writes failed, socket busted\n";
    break;
  }

  return stat;
}

WriteStatus Socket::write(const wire::Message& msg) {
  std::string out;

  if(!msg.SerializeToString(&out)) {
    std::cerr << "Error serializing message\n";
    return eFailure;
  }

  return write_with_size(out);
}

void Socket::set_nonblock() {
  int flags = fcntl(fd, F_GETFL, 0);
  int r = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  assert(0 <= r && "Setting socket non-block failed!");
}

