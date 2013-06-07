#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <vector>
#include <list>
#include <string>
#include <map>

#include <ev++.h>

#include "harq.hpp"
#include "buffer.hpp"
#include "socket.hpp"

#include "http_parser.h"
#include "http.pb.h"

class Server;

enum DeliverStatus { eIgnored, eWaitForAck, eConsumed };

class Connection {
public:
  enum State { eReadSize, eReadMessage };

private:
  int id_;
  Socket sock_;
  ev::io read_w_;
  ev::io write_w_;

  bool open_;
  Server& server_;

  Buffer buffer_;

  State state_;

  int need_;

  bool writer_started_;

  int inflight_max_;

  http_parser parser_;
  http_parser_settings settings_;

  http::Request req_;

  enum HeaderState { eNone, eField, eValue } hstate_;
  std::string field_;
  std::string value_;

  bool set_body_;
  std::string body_;

  bool expect_100_;

public:
  /*** methods ***/

  Connection(Server& s, int id, int fd);
  ~Connection();

  Buffer& buffer() {
    return buffer_;
  }

  int id() {
    return id_;
  }

  bool write(wire::Message& msg);

  bool write(const std::string& str);

  void on_readable(ev::io& w, int revents);
  void on_queue_readable(ev::io& w, int revents);
  void on_writable(ev::io& w, int revents);

  void start();
  void start_queue();

  void cleanup();

  void clear();
  void set_url(std::string u);
  void set_field(std::string f);
  void set_value(std::string v);
  void set_body(std::string b);
  void flush_headers();
  void flush();

private:
  void reopen_queue();
  void signal_cleanup();

  void handle_message(const wire::Message& msg);
};

#endif
