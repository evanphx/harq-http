#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <netinet/in.h>  /* inet_ntoa */
#include <arpa/inet.h>   /* inet_ntoa */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include <iostream>
#include <sstream>

#include "debugs.hpp"
#include "util.hpp"
#include "server.hpp"
#include "connection.hpp"

#include "wire.pb.h"

#include "flags.hpp"
#include "types.hpp"

#include "option.hpp"

#define EVBACKEND EVFLAG_AUTO

#ifdef __linux
#undef EVBACKEND
#define EVBACKEND EVBACKEND_EPOLL
#endif

#ifdef __APPLE__
#undef EVBACKEND
#define EVBACKEND EVBACKEND_KQUEUE
#endif

Server::Server(std::string db_path, std::string hostaddr, int port)
    : db_path_(db_path)
    , hostaddr_(hostaddr)
    , port_(port)
    , fd_(-1)
    , loop_(EVBACKEND)
    , connection_watcher_(loop_)
    , sigint_watcher_(loop_)
    , sigterm_watcher_(loop_)
    , cleanup_watcher_(loop_)
    , next_id_(0)
{
  sigint_watcher_.set<Server, &Server::on_signal>(this);
  sigint_watcher_.start(SIGINT);

  sigterm_watcher_.set<Server, &Server::on_signal>(this);
  sigterm_watcher_.start(SIGTERM);

  cleanup_watcher_.set<Server, &Server::cleanup>(this);
  cleanup_watcher_.start();
}

Server::~Server() {
  close(fd_);
}

void Server::cleanup(ev::check& w, int revents) {
  // Now all the closing connections are detached from queues
  // and this in the only reference left to them, so we can have
  // them flush their un-ack'd messages safely and then delete them.

  for(Connections::iterator i = closing_connections_.begin();
      i != closing_connections_.end();
      ++i) {
    (*i)->cleanup();
    delete *i;
  }

  closing_connections_.clear();
}

void Server::start() {    
  if((fd_ = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket()");
    exit(1);
  }

  int flags = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
  setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));

  struct linger ling = {0, 0};
  setsockopt(fd_, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));

  /* XXX: Sending single byte chunks in a response body? Perhaps there is a
   * need to enable the Nagel algorithm dynamically. For now disabling.
   */
  setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));

  struct sockaddr_in addr;

  /* the memset call clears nonstandard fields in some impementations that
   * otherwise mess things up.
   */
  memset(&addr, 0, sizeof(addr));

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);

  if(!hostaddr_.empty()) {
    addr.sin_addr.s_addr = inet_addr(hostaddr_.c_str());
    if(addr.sin_addr.s_addr == INADDR_NONE){
      printf("Bad address(%s) to listen\n",hostaddr_.c_str());
      exit(1);
    }
  } else {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  }

  if(bind(fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind()");
    if(fd_ > 0) close(fd_);
    exit(1);
  }

  if(listen(fd_, MAX_CONNECTIONS) < 0) {
    perror("listen()");
    exit(1);
  }

  set_nonblock(fd_);

  connection_watcher_.set<Server, &Server::on_connection>(this);
  connection_watcher_.start(fd_, EV_READ);

  loop_.run(0);
}

void Server::on_signal(ev::sig& w, int revents) {
  std::cerr << "Exitting...\n";
  loop_.break_loop();
}

void Server::on_connection(ev::io& w, int revents) {
  if(EV_ERROR & revents) {
    puts("on_connection() got error event, closing server.");
    return;
  }

  struct sockaddr_in addr; // connector's address information
  socklen_t addr_len = sizeof(addr); 
  int fd = accept(fd_, (struct sockaddr*)&addr, &addr_len);

  if(fd < 0) {
    perror("accept()");
    return;
  }

  Connection* connection = new Connection(ref(this), fd);

  if(connection == NULL) {
    close(fd);
    return;
  }

  connections_.push_back(connection);

  connection->start();
}

void Server::deliver(http::Request& req) {
  /*
  google::protobuf::io::OstreamOutputStream out(&std::cerr);
  google::protobuf::TextFormat::Print(req_, &out);
  std::cout << "done\n";
  */

  wire::Message msg;
  msg.set_destination("/harq-http");
  msg.set_payload(req.SerializeAsString());

  queue_->write(msg);
}

void Server::connect(std::string host, int c_port) {
  int s, rv;
  char port[6];  /* strlen("65535"); */
  struct addrinfo hints, *servinfo, *p;

  snprintf(port, 6, "%d", c_port);
  memset(&hints,0,sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(host.c_str(), port, &hints, &servinfo)) != 0) {
    printf("Error: %s\n", gai_strerror(rv));
    return;
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
      continue;

    if (::connect(s,p->ai_addr,p->ai_addrlen) == -1) {
      close(s);
      continue;
    }

    goto end;
  }

  if (p == NULL) {
    printf("Can't create socket: %s\n",strerror(errno));
    return;
  }

end:
  freeaddrinfo(servinfo);

  Connection* con = new Connection(ref(this), s);

  if(con == NULL) {
    close(s);
    return;
  }

  queue_ = con;

  connections_.push_back(con);

  con->start_queue();
}
