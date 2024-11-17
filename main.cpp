#include "session.hpp"
#include <log/log.hpp>

void doAccept(tcp::acceptor &acceptor)
{
  acceptor.async_accept([&](boost::system::error_code ec, tcp::socket socket) {
    if (ec)
    {
      LOG("Accept failed:", ec.message());
      doAccept(acceptor);
      return;
    }
    std::make_shared<Session>(std::move(socket))->run();
    doAccept(acceptor);
  });
}

auto main() -> int
{
  try
  {
    auto ioc = boost::asio::io_context{1};
    auto endpoint = tcp::endpoint{tcp::v4(), 8090};
    auto acceptor = tcp::acceptor{ioc, endpoint};
    doAccept(acceptor);
    ioc.run();
  }
  catch (const std::exception &e)
  {
    LOG("Error:", e.what());
  }
}
