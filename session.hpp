#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;

// Session class to handle both HTTP and WebSocket sessions
class Session : public std::enable_shared_from_this<Session>
{
public:
  Session(tcp::socket socket);
  auto run() -> void;

private:
  tcp::socket socket;
  boost::asio::strand<boost::asio::any_io_executor> strand;
  boost::beast::flat_buffer buffer;
  http::request<http::string_body> req;

  auto doRead() -> void;
  auto handleRequest() -> void;
  auto handleHttpRequest() -> void;
  auto sendNotFoundResponse(const std::string &target) -> void;
};
