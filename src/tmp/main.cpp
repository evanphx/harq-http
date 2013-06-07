#include "http.pb.h"
#include "http_parser.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#define EVBACKEND EVFLAG_AUTO

#ifdef __linux
#undef EVBACKEND
#define EVBACKEND EVBACKEND_EPOLL
#endif

#ifdef __APPLE__
#undef EVBACKEND
#define EVBACKEND EVBACKEND_KQUEUE
#endif

void set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  int r = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  assert(0 <= r && "Setting socket non-block failed!");
}

class Server {
  ev::dynamic_loop loop_;
  ev::io con_watcher_;
  int fd_;

public:
  Server()
    : loop_(EVBACKEND)
    , con_watcher_(loop_)
    , fd_(0)
  {}

  ev::dynamic_loop& loop() {
    return loop_;
  }

  void start(int port) {
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

    con_watcher_.set<Server, &Server::on_connection>(this);
    con_watcher_.start(fd_, EV_READ);

    loop_.run(0);
  }

  void on_connection(ev::io& w, int revents) {
    if(EV_ERROR & revents) {
      std::cerr << "connection error event!\n";
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
}

class Connection {
  Server* server_;
  int fd_;
  ev::io read_w_;
  ev::io write_w_;

public:
  Connection(Server* s, int fd)
    : server_(s)
    , fd_(fd)
    , read_w_(s.loop())
    , write_w_(s.loop())
  {
    read_w_.set<Connection, &Connection::on_readable>(this);
    write_w_.set<Connection, &Connection::on_writable>(this);

    set_nonblock(fd_);
  }

  ~Connection() {
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

  void Connection::start() {
    read_w_.start(sock_.fd, EV_READ);
  }

};

int main(int argc, char** argv) {
  Server hndl;
  hndl.start(8080);
}
