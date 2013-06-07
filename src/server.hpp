#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <list>
#include <string>
#include <map>

#include <iostream>

#include "ev++.h"
#include "debugs.hpp"
#include "safe_ref.hpp"

#include "option.hpp"

class Connection;

typedef std::list<Connection*> Connections;

namespace http {
  class Request;
}

enum DataStatus {
  eMissing,
  eValid,
  eInvalid
};

class Server {
  std::string db_path_;
  std::string hostaddr_;
  int port_;
  int fd_;

  ev::dynamic_loop loop_;
  ev::io connection_watcher_;
  ev::sig sigint_watcher_;
  ev::sig sigterm_watcher_;
  ev::check cleanup_watcher_;

  Connections connections_;
  Connections replicas_;
  Connections taps_;

  Connections closing_connections_;

  uint64_t next_id_;

  Connection* queue_;

public:

  ev::dynamic_loop& loop() {
    return loop_;
  }

  void remove_connection(Connection* con) {
    connections_.remove(con);
    closing_connections_.push_back(con);
  }

  void add_replica(Connection* con) {
    replicas_.push_back(con);
  }

  void add_tap(Connection* con) {
    taps_.push_back(con);
  }

  uint64_t next_id() {
    return ++next_id_;
  }

  std::string dname(std::string queue) {
    return std::string("-") + queue;
  }

  bool read_queues();

  Server(std::string db_path, std::string hostaddr, int port);
  ~Server();
  void start();
  void on_connection(ev::io& w, int revents);

  void on_signal(ev::sig& w, int revents);
  void cleanup(ev::check& w, int revents);

  void connect(std::string host, int c_port);
  void deliver(http::Request& req_);
};


#endif

