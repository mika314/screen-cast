#include "session.hpp"
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <log/log.hpp>
#include <thread>
#include <unistd.h>

// Forward declaration
void do_accept(tcp::acceptor &acceptor);

// Start accepting connections
void do_accept(tcp::acceptor &acceptor)
{
  acceptor.async_accept([&](boost::system::error_code ec, tcp::socket socket) {
    if (!ec)
    {
      std::make_shared<Session>(std::move(socket))->run();
    }
    else
    {
      LOG("Accept failed:", ec.message());
    }
    do_accept(acceptor);
  });
}

int main()
{
  try
  {
    boost::asio::io_context ioc{1};
    tcp::endpoint endpoint{tcp::v4(), 8090};

    tcp::acceptor acceptor{ioc, endpoint};
    do_accept(acceptor);

    ioc.run();
  }
  catch (const std::exception &e)
  {
    LOG("Error:", e.what());
  }

  return 0;
}
