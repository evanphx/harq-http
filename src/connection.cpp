#include <algorithm>
#include <iostream>

#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

#include "util.hpp"
#include "server.hpp"
#include "connection.hpp"
#include "action.hpp"

#include "wire.pb.h"

#include "debugs.hpp"

#include "http_parser.h"

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#define FLOW(str) debugs << "- " << str << "\n"

#define C ((Connection*)(p->data))

int cb_begin(http_parser* p) {
  C->clear();
  return 0;
}

int cb_url(http_parser* p, const char* at, size_t len) {
  C->set_url(std::string(at, len));
  return 0;
}

int cb_status(http_parser* p) {

  return 0;
}

int cb_field(http_parser* p, const char* at, size_t len) {
  C->set_field(std::string(at, len));
  return 0;
}

int cb_value(http_parser* p, const char* at, size_t len) {
  C->set_value(std::string(at, len));
  return 0;
}

int cb_headers(http_parser* p) {
  C->flush_headers();
  return 0;
}

int cb_body(http_parser* p, const char* at, size_t len) {
  C->set_body(std::string(at, len));
  return 0;
}

int cb_done(http_parser* p) {
  C->flush();
  return 0;
}

Connection::Connection(Server& s, int id, int fd)
  : id_(id)
  , sock_(fd)
  , read_w_(s.loop())
  , write_w_(s.loop())
  , open_(true)
  , server_(s)
  , buffer_(1024)
  , state_(eReadSize)
  , writer_started_(false)
  , inflight_max_(1)
  , hstate_(eNone)
  , set_body_(false)
{
  read_w_.set<Connection, &Connection::on_readable>(this);
  write_w_.set<Connection, &Connection::on_writable>(this);

  sock_.set_nonblock();

  http_parser_init(&parser_, HTTP_REQUEST);
  parser_.data = this;

  settings_.on_message_begin = cb_begin;
  settings_.on_url = cb_url;
  settings_.on_status_complete = cb_status;
  settings_.on_header_field = cb_field;
  settings_.on_header_value = cb_value;
  settings_.on_headers_complete = cb_headers;
  settings_.on_body = cb_body;
  settings_.on_message_complete = cb_done;
}

Connection::~Connection() {
  if(open_) {
    read_w_.stop();

    if(writer_started_) {
      write_w_.stop();
    }

    for(;;) {
      int ret = close(sock_.fd);
      if(ret == 0) break;
      if(errno == EINTR) continue;

      std::cerr << "Error while closing fd " << sock_.fd
                << " (" << errno << ", " << strerror(errno) << ")\n";
      break;
    }
  }
}

void Connection::clear() {}
void Connection::set_url(std::string u) {
  req_.set_url(u);
}

#define LOWER(c)            (unsigned char)(c | 0x20)

static option<http::Header_Key> standard(std::string h) {
  switch(LOWER(h[0])) {
  case 'h':
    if(strcasecmp(h.data(), "host") == 0) {
      return http::Header_Key_HOST;
    }

    break;
  case 'a':
    if(strcasecmp(h.data(), "accept")) {
      return http::Header_Key_ACCEPT;
    }
    break;
  case 'u':
    if(strcasecmp(h.data(), "user-agent")) {
      return http::Header_Key_USER_AGENT;
    }
    break;
  }

  return option<http::Header_Key>();
}

static bool expect_100(std::string& f, std::string& v) {
  return strcasecmp(f.data(), "expect") == 0 &&
         strcasecmp(v.data(), "100-continue") == 0;
}

void Connection::set_field(std::string f) {
  if(hstate_ == eValue) {
    http::Header* r = req_.add_headers();
    if(option<http::Header_Key> k = standard(field_)) {
      r->set_key(*k);
    } else {
      r->set_custom_key(field_);
    }

    r->set_value(value_);

    if(!expect_100_) expect_100_ = expect_100(field_, value_);

    value_ = "";
    field_ = f;
  } else {
    field_ += f;
  }

  hstate_ = eField;
}

void Connection::set_value(std::string v) {
  value_ += v;
  hstate_ = eValue;
}

void Connection::flush_headers() {
  if(hstate_ == eValue) {
    http::Header* r = req_.add_headers();
    if(option<http::Header_Key> k = standard(field_)) {
      r->set_key(*k);
    } else {
      r->set_custom_key(field_);
    }

    r->set_value(value_);

    if(!expect_100_) expect_100_ = expect_100(field_, value_);
  }

  if(expect_100_) {
    static std::string sContinue("HTTP/1.1 100 Continue\r\n\r\n");
    sock_.write(sContinue);
  }
}

void Connection::set_body(std::string b) {
  set_body_ = true;
  body_ += b;
}

option<http::Request_Method> req_enum(unsigned char n) {
  switch(n) {
  case HTTP_DELETE:
    return http::Request_Method_DELETE;
  case HTTP_GET:
    return http::Request_Method_GET;
  case HTTP_HEAD:
    return http::Request_Method_HEAD;
  case HTTP_POST:
    return http::Request_Method_POST;
  case HTTP_PUT:
    return http::Request_Method_PUT;
  default:
    return option<http::Request_Method>();
  }
}

void Connection::flush() {
  req_.set_stream_id(id_);
  req_.set_version_major(parser_.http_major);
  req_.set_version_minor(parser_.http_minor);

  if(set_body_) {
    req_.set_body(body_);
  }

  if(option<http::Request_Method> m = req_enum(parser_.method)) { 
    req_.set_method(*m);
  } else {
    req_.set_custom_method(http_method_str((http_method)parser_.method));
  }

  server_.deliver(req_);
}

void Connection::start() {
  FLOW("New Connection");
  read_w_.start(sock_.fd, EV_READ);
}

void Connection::start_queue() {
  read_w_.set<Connection, &Connection::on_queue_readable>(this);
  read_w_.start(sock_.fd, EV_READ);
}

void Connection::reopen_queue() {
  server_.remove_connection(this);
  read_w_.stop();
  write_w_.stop();

  close(sock_.fd);
}

void Connection::on_queue_readable(ev::io& w, int revents) {
  if(EV_ERROR & revents) {
    std::cerr << "Error event detected, closing connection\n";
    reopen_queue();
    return;
  }

  ssize_t recved = buffer_.fill(sock_.fd);

  if(recved < 0) {
    if(errno == EAGAIN || errno == EWOULDBLOCK) return;
    debugs << "Error reading from socket: " << strerror(errno) << "\n";
    reopen_queue();
    return;
  }

  if(recved == 0) {
    reopen_queue();
    return;
  }

  debugs << "Read " << recved << " bytes\n";

  // Allow us to parse multiple messages in one read
  for(;;) {
    if(state_ == eReadSize) {
      FLOW("READ SIZE");

      debugs << "avail=" << buffer_.read_available() << "\n";

      if(buffer_.read_available() < 4) return;

      int size = buffer_.read_int32();

      debugs << "msg size=" << size << "\n";

      need_ = size;

      state_ = eReadMessage;
    }

    debugs << "avail=" << buffer_.read_available() << "\n";

    if(buffer_.read_available() < need_) {
      FLOW("NEED MORE");
      return;
    }

    FLOW("READ MSG");

    wire::Message msg;

    bool ok = msg.ParseFromArray(buffer_.read_pos(), need_);

    buffer_.advance_read(need_);

    if(ok) {
      handle_message(msg);
    } else {
      std::cerr << "Unable to parse request\n";
      reopen_queue();
      return;
    }

    state_ = eReadSize;
  }
}

void Connection::handle_message(const wire::Message& msg) {
  http::Response rep;

  if(!rep.ParseFromString(msg.payload())) {
    std::cerr << "Get malformed response\n";
  }

  server_.send_reply(rep);
}

void Connection::on_readable(ev::io& w, int revents) {
  ssize_t s = buffer_.fill(sock_.fd);

  if(s == 0) {
    read_w_.stop();
    write_w_.stop();

    close(sock_.fd);
    return;
  }

  size_t read = http_parser_execute(&parser_, &settings_,
                   (const char*)buffer_.read_pos(), buffer_.read_available());

  buffer_.advance_read(read);
}

void Connection::cleanup() {
}

void Connection::signal_cleanup() {
  server_.remove_connection(this);
}

void Connection::on_writable(ev::io& w, int revents) {
  FLOW("WRITE READY");

  switch(sock_.flush()) {
  case eOk:
    debugs << "Flushed socket in writable event\n";
    writer_started_ = false;
    write_w_.stop();
    return;
  case eFailure:
    std::cerr << "Error writing to socket in writable event\n";
    signal_cleanup();
    writer_started_ = false;
    write_w_.stop();
    return;
  case eWouldBlock:
    debugs << "Flush didn't finish for writeable event\n";
    return;
  }
}

bool Connection::write(wire::Message& msg) {
  switch(sock_.write(msg)) {
  case eOk:
    return true;
  case eFailure:
    debugs << "Error writing to socket\n";
    signal_cleanup();
    return false;
  case eWouldBlock:
    writer_started_ = true;
    write_w_.start(sock_.fd, EV_WRITE);
    debugs << "Starting writable watcher\n";
    return true;
  }
}

bool Connection::write(const std::string& str) {
  switch(sock_.write(str)) {
  case eOk:
    return true;
  case eFailure:
    debugs << "Error writing to socket\n";
    signal_cleanup();
    return false;
  case eWouldBlock:
    writer_started_ = true;
    write_w_.start(sock_.fd, EV_WRITE);
    debugs << "Starting writable watcher\n";
    return true;
  }
}
