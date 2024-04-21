// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include "PostStorageService.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace  ::social_network;

class PostStorageServiceHandler : virtual public PostStorageServiceIf {
 public:
  PostStorageServiceHandler() {
    // Your initialization goes here
  }

  void StorePost(const int8_t flow, const int64_t timestamp2, const int16_t edge, const int64_t req_id, const Post& post, const std::map<std::string, std::string> & carrier) {
    // Your implementation goes here
    printf("StorePost\n");
  }

  void ReadPost(Post& _return, const int8_t flow, const int64_t timestamp2, const int16_t edge, const int64_t req_id, const int64_t post_id, const std::map<std::string, std::string> & carrier) {
    // Your implementation goes here
    printf("ReadPost\n");
  }

  void ReadPosts(std::vector<Post> & _return, const int8_t flow, const int64_t timestamp2, const int16_t edge, const int64_t req_id, const std::vector<int64_t> & post_ids, const std::map<std::string, std::string> & carrier) {
    // Your implementation goes here
    printf("ReadPosts\n");
  }

};

int main(int argc, char **argv) {
  int port = 9090;
  ::apache::thrift::stdcxx::shared_ptr<PostStorageServiceHandler> handler(new PostStorageServiceHandler());
  ::apache::thrift::stdcxx::shared_ptr<TProcessor> processor(new PostStorageServiceProcessor(handler));
  ::apache::thrift::stdcxx::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
  ::apache::thrift::stdcxx::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  ::apache::thrift::stdcxx::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

  TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);
  server.serve();
  return 0;
}
