#include <signal.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../../gen-cpp/social_network_types.h"
#include "../ClientPool.h"
#include "../utils.h"
#include "../utils_thrift.h"
#include "Ping4Handler.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();
  //SetUpTracer("config/jaeger-config.yml", "compose-post-service");

  json config_json;
  if (load_config_file("config/service-config.json", &config_json) != 0) {
    exit(EXIT_FAILURE);
  }
  
  int port, next_conns, keepalive, this_port;
  std::string ping_addr;
  
     // Instantiate the ping servers.
     this_port = config_json["ping-service-4"]["port"];
     ping_addr = config_json["ping-service-5"]["addr"];
     port = config_json["ping-service-5"]["port"];
     next_conns = config_json["ping-service-5"]["connections"];
     keepalive = config_json["ping-service-5"]["keepalive_ms"];

     ClientPool<ThriftClient<Ping5ServiceClient>> pong_client_pool("pong-client", ping_addr, port, 0, 
		     next_conns, 10000, keepalive, config_json);

     std::shared_ptr<TServerSocket> server_socket = get_server_socket(config_json, "0.0.0.0", this_port);
     TThreadedServer server(
      std::make_shared<Ping4ServiceProcessor>(
          std::make_shared<Ping4Handler>(&pong_client_pool)), 
          server_socket,
          std::make_shared<TFramedTransportFactory>(),
          std::make_shared<TBinaryProtocolFactory>());
     
     LOG(info) << "Starting the ping server 0 ...";
     server.serve();
}
