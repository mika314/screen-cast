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
  void run();

private:
  tcp::socket socket_;
  boost::asio::strand<boost::asio::any_io_executor> strand_;
  boost::beast::flat_buffer buffer_;
  http::request<http::string_body> req_;

  void do_read();
  void handle_request();
  void handle_http_request();
  void send_not_found_response(const std::string &target);
};
