#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>

#include <iostream>

#include "util.hpp"
#include "server.hpp"
#include "connection.hpp"
#include "config.hpp"

extern char *optarg;

Server *server=NULL;

int main(int argc, char** argv) {
  bool daemon = false;

  std::string host = "";

  int port=7622;
  int master_port = -1;

  std::string data_dir = "harq.db";

  int ch = 0;
  while((ch = getopt(argc, argv, "hDb:p:d:m:")) != -1) {
    switch(ch) {
    default:
    case 'h':
      std::cout
        << "Usage:\n\t./harq [options]\n"
        << "Options:\n"
        << "\t-D:\t\t daemon\n"
        << "\t-b host-ip:\t listen host\n"
        << "\t-p port:\t listen port\n"
        << "\t-d data-dir:\t data dir\n"
        << "\t-m master:\t master\n";

      exit(0);
    case 'D':
      daemon = true;
      break;
    case 'b':
      host = optarg;
      break;
    case 'p':
      port = (int)strtol(optarg, (char **)NULL, 10);
      if(!port){
        printf("Bad port(-p) value\n");
        exit(1);
      }
      break;
    case 'd':
      data_dir = optarg;
      break;
    case 'm':
      master_port = atoi(optarg);
      break;
    }
  }

  if(daemon) {
    if(daemon_init() == -1) { 
      printf("can't run as daemon\n"); 
      exit(1);
    }
  }

  // signal(SIGTERM, sig_term);
  // signal(SIGINT,  sig_term);
  signal(SIGPIPE, SIG_IGN);

  /*
  Config cfg("qadmus.cfg");
  cfg.open();
  if(!cfg.read()) {
    std::cout << "Config error: " << cfg.error() << "\n";
  }

  cfg.show();
  */

  Server server(data_dir, host, port);
  server.connect("127.0.0.1", 7621);
  server.start();

  return 0;
}

