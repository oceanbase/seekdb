/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "lib/oblog/ob_log_module.h"
#include "grpc/example.grpc.pb.h"
#include "lib/utility/ob_test_util.h"
#include "grpc/ob_grpc_context.h"
#include <gtest/gtest.h>
#include "rpc/frame/ob_req_transport.h"
#include "lib/coro/testing.h"
#include <linux/futex.h>
#include "grpc/ob_grpc_server.h"
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <grpcpp/grpcpp.h>

namespace oceanbase
{
using namespace common;
using namespace example;

namespace unittest
{

class TestGrpc : public ::testing::Test
{
public:
  TestGrpc() {
    addr.set_ip_addr("127.0.0.1", 8001);
  }
  virtual ~TestGrpc() {}
public:
  virtual void SetUp();
  virtual void TearDown();
  ObAddr addr;
};

const int64_t RPC_TIMEOUT = 2000 * 1000 * 1000L;

void TestGrpc::SetUp()
{

}

void TestGrpc::TearDown()
{
}

using example::HelloRequest;
using example::HelloReply;

class ExampleServiceImpl final : public example::ExampleService::Service {
public:
    // Unary RPC
    grpc::Status SayHello(grpc::ServerContext* context, const HelloRequest* request,
                    HelloReply* reply) override {
      std::string prefix("Hello ");
      reply->set_message(prefix + request->name());
      std::cout << "Received SayHello request from " << request->name() << std::endl;
      return grpc::Status::OK;
    }

    // Server streaming RPC: send multiple replies
    grpc::Status SayHelloServerStream(grpc::ServerContext* context, const HelloRequest* request,
                                grpc::ServerWriter<HelloReply>* writer) override {
      std::cout << "Received SayHelloServerStream request from " << request->name() << std::endl;
      for (int i = 0; i < 5; ++i) {
        HelloReply reply;
        reply.set_message("Hello " + request->name() + " #" + std::to_string(i + 1));
        if (!writer->Write(reply)) {
          std::cout << "Failed to write reply #" << i + 1 << std::endl;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      return grpc::Status::OK;
    }

    // Client streaming RPC: receive multiple requests
    grpc::Status SayHelloClientStream(grpc::ServerContext* context,
                                grpc::ServerReader<HelloRequest>* reader,
                                HelloReply* reply) override {
      std::cout << "Received SayHelloClientStream request" << std::endl;
      HelloRequest request;
      std::string all_names;
      int count = 0;
      while (reader->Read(&request)) {
        if (count > 0) {
          all_names += ", ";
        }
        all_names += request.name();
        count++;
        std::cout << "Received name: " << request.name() << std::endl;
      }
      reply->set_message("Hello to all: " + all_names + " (total: " + std::to_string(count) + ")");
      return grpc::Status::OK;
    }

    // Bidirectional streaming RPC: both client and server stream
    grpc::Status SayHelloBidiStream(grpc::ServerContext* context,
                              grpc::ServerReaderWriter<HelloReply, HelloRequest>* stream) override {
      std::cout << "Received SayHelloBidiStream request" << std::endl;
      HelloRequest request;
      int count = 0;
      while (stream->Read(&request)) {
        count++;
        HelloReply reply;
        reply.set_message("Echo: Hello " + request.name() + " (message #" + std::to_string(count) + ")");
        if (!stream->Write(reply)) {
          std::cout << "Failed to write reply for " << request.name() << std::endl;
          break;
        }
        std::cout << "Echoed: " << request.name() << std::endl;
      }
      return grpc::Status::OK;
    }

};

TEST_F(TestGrpc, base)
{
  oceanbase::obgrpc::ObGrpcClient<example::ExampleService> grpc_client_;
  ObAddr bad_addr;
  bad_addr.set_ip_addr("127.0.0.1", 0);
  ASSERT_EQ(OB_INVALID_ARGUMENT, grpc_client_.init(bad_addr, RPC_TIMEOUT));
}

TEST_F(TestGrpc, unary)
{
  int64_t start_ts = oceanbase::ObTimeUtility::current_time();
  std::cout << "Echoed: " << start_ts << std::endl;
  oceanbase::obgrpc::ObGrpcClient<example::ExampleService> grpc_client_;
  int ret = grpc_client_.init(addr, RPC_TIMEOUT);
  ASSERT_EQ(OB_SUCCESS, ret);
  example::HelloRequest req;
  req.set_name("Grpc");
  example::HelloReply rep;
  grpc::ClientContext context;
  GRPC_SET_CONTEXT(grpc_client_, context);
  auto status = grpc_client_.stub_->SayHello(&context, req, &rep);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(rep.message(), std::string("Hello Grpc"));
  int64_t end_ts = oceanbase::ObTimeUtility::current_time();
  std::cout << "Echoed: " << end_ts << std::endl;
}

TEST_F(TestGrpc, server_stream)
{
  oceanbase::obgrpc::ObGrpcClient<example::ExampleService> grpc_client_;

  int ret = grpc_client_.init(addr, RPC_TIMEOUT);
  ASSERT_EQ(OB_SUCCESS, ret);

  example::HelloRequest req;
  req.set_name("StreamTest");

  grpc::ClientContext context;
  GRPC_SET_CONTEXT(grpc_client_, context);
  auto reader = grpc_client_.stub_->SayHelloServerStream(&context, req);

  example::HelloReply reply;
  int count = 0;
  while (reader->Read(&reply)) {
    count++;
    std::string expected = "Hello StreamTest #" + std::to_string(count);
    ASSERT_EQ(reply.message(), expected);
    std::cout << "Received: " << reply.message() << std::endl;
  }

  auto status = reader->Finish();
  ASSERT_EQ(OB_SUCCESS, grpc_client_.translate_error(status));
  ASSERT_EQ(count, 5);
}

TEST_F(TestGrpc, client_stream)
{
  oceanbase::obgrpc::ObGrpcClient<example::ExampleService> grpc_client_;

  int ret = grpc_client_.init(addr, RPC_TIMEOUT);
  ASSERT_EQ(OB_SUCCESS, ret);

  grpc::ClientContext context;
  example::HelloReply reply;
  GRPC_SET_CONTEXT(grpc_client_, context);
  auto writer = grpc_client_.stub_->SayHelloClientStream(&context, &reply);

  std::vector<std::string> names = {"Alice", "Bob", "Charlie"};
  for (const auto& name : names) {
    example::HelloRequest req;
    req.set_name(name);
    ASSERT_TRUE(writer->Write(req));
    std::cout << "Sent: " << name << std::endl;
  }

  writer->WritesDone();
  auto status = writer->Finish();
  ASSERT_EQ(OB_SUCCESS, grpc_client_.translate_error(status));

  ASSERT_TRUE(reply.message().find("Hello to all:") != std::string::npos);
  ASSERT_TRUE(reply.message().find("Alice") != std::string::npos);
  ASSERT_TRUE(reply.message().find("Bob") != std::string::npos);
  ASSERT_TRUE(reply.message().find("Charlie") != std::string::npos);
  ASSERT_TRUE(reply.message().find("total: 3") != std::string::npos);
  std::cout << "Received reply: " << reply.message() << std::endl;
}

TEST_F(TestGrpc, BIDI_STREAM)
{
  oceanbase::obgrpc::ObGrpcClient<example::ExampleService> grpc_client_;

  int ret = grpc_client_.init(addr, RPC_TIMEOUT);
  ASSERT_EQ(OB_SUCCESS, ret);

  grpc::ClientContext context;
  GRPC_SET_CONTEXT(grpc_client_, context);
  auto stream = grpc_client_.stub_->SayHelloBidiStream(&context);

  std::vector<std::string> names = {"User1", "User2", "User3"};
  std::thread writer_thread([&stream, &names]() {
    for (const auto& name : names) {
      example::HelloRequest req;
      req.set_name(name);
      ASSERT_TRUE(stream->Write(req));
      std::cout << "Sent: " << name << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    stream->WritesDone();
  });

  int count = 0;
  example::HelloReply reply;
  while (stream->Read(&reply)) {
    count++;
    std::cout << "Received: " << reply.message() << std::endl;
    ASSERT_TRUE(reply.message().find("Echo: Hello") != std::string::npos);
  }

  writer_thread.join();
  auto status = stream->Finish();
  ASSERT_EQ(OB_SUCCESS, grpc_client_.translate_error(status));
  ASSERT_EQ(count, 3);
}

// Async Unary RPC test with callback
TEST_F(TestGrpc, async_unary)
{
  oceanbase::obgrpc::ObGrpcClient<example::ExampleService> grpc_client_;
  int ret = grpc_client_.init(addr, RPC_TIMEOUT);
  ASSERT_EQ(OB_SUCCESS, ret);
  example::HelloRequest req;
  req.set_name("AsyncGrpc");
  grpc::ClientContext context;
  GRPC_SET_CONTEXT(grpc_client_, context);

  // Use mutex and condition variable to wait for callback
  std::mutex mtx;
  std::condition_variable cv;
  struct CallBack {
  example::HelloReply rep;
    bool called = false;
    grpc::Status status;
  };
  CallBack cb;
  // Send async request with callback - client sends and doesn't wait
  grpc_client_.stub_->async()->SayHello(
      &context,
      &req,
      &cb.rep,
      [&mtx, &cv, &cb](grpc::Status status) {
        std::lock_guard<std::mutex> lock(mtx);
        cb.called = true;
        cb.status = status;
        cv.notify_one();
      });

  // Client can do other work here without blocking
  std::cout << "Client sent async request, doing other work..." << std::endl;

  // Wait for callback to be invoked (in real scenario, this would be in event loop)
  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [&cb] { return cb.called; });

  // Check result in callback
  ASSERT_TRUE(cb.status.ok());
  ASSERT_EQ(cb.rep.message(), std::string("Hello AsyncGrpc"));
  std::cout << "Async unary RPC callback executed: " << cb.rep.message() << std::endl;
}

} // end of unittest
} // end of oceanbase

int main(int argc, char **argv)
{
  oceanbase::obgrpc::ObGrpcServer grpc_server;
  oceanbase::unittest::ExampleServiceImpl service;
  grpc_server.register_service(&service);
  grpc_server.start(8001);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc,argv);
  int rc = RUN_ALL_TESTS();
  grpc_server.stop();
  grpc_server.wait();
  return rc;
}
