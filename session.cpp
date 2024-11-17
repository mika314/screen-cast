#include "session.hpp"
#include "web-socket-session.hpp"
#include <fstream>
#include <log/log.hpp>

namespace websocket = boost::beast::websocket;

Session::Session(tcp::socket socket) : socket_(std::move(socket)), strand_(socket_.get_executor()) {}

void Session::run()
{
  LOG("Read a request");
  do_read();
}

void Session::do_read()
{
  auto self = shared_from_this();
  http::async_read(socket_,
                   buffer_,
                   req_,
                   boost::asio::bind_executor(
                     strand_, [self](boost::system::error_code ec, std::size_t bytes_transferred) {
                       boost::ignore_unused(bytes_transferred);
                       if (!ec)
                         self->handle_request();
                     }));
}

void Session::handle_request()
{
  LOG("Handle WebSocket handshake");
  if (websocket::is_upgrade(req_))
  {
    LOG("Create WebSocket session and transfer ownership of the socket");
    std::make_shared<WebSocketSession>(std::move(socket_))->run(std::move(req_));
    return;
  }

  LOG("Handle HTTP request");
  handle_http_request();
}

void Session::handle_http_request()
{
  LOG("Build the path to the requested file");
  std::string path = req_.target().to_string();
  if (path == "/")
    path = "/index.html";

  std::string full_path = "." + path; // Assuming files are in the current directory

  LOG("Open the file", full_path);
  std::ifstream file(full_path, std::ios::in | std::ios::binary);
  if (!file)
  {
    LOG("File not found, send 404 response");
    send_not_found_response(path);
    return;
  }

  LOG("Read the file content");
  std::stringstream ss;
  ss << file.rdbuf();
  std::string content = ss.str();

  LOG("Determine the content type");
  std::string content_type;
  if (path.ends_with(".html"))
    content_type = "text/html";
  else if (path.ends_with(".js"))
    content_type = "application/javascript";
  else if (path.ends_with(".css"))
    content_type = "text/css";
  else
    content_type = "application/octet-stream";

  LOG("Build the response");
  // Use a shared_ptr to extend the lifetime of res
  auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req_.version());
  res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res->set(http::field::content_type, content_type);
  res->keep_alive(req_.keep_alive());
  res->body() = content;
  res->prepare_payload();

  LOG("Send the response", content);
  auto self = shared_from_this();
  http::async_write(
    socket_,
    *res,
    boost::asio::bind_executor(strand_, [self, res](boost::system::error_code ec, std::size_t) {
      LOG("response sent");
      self->socket_.shutdown(tcp::socket::shutdown_send, ec);
    }));
}

void Session::send_not_found_response(const std::string &target)
{
  LOG("Build the 404 response");
  auto res =
    std::make_shared<http::response<http::string_body>>(http::status::not_found, req_.version());
  res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res->set(http::field::content_type, "text/html");
  res->keep_alive(req_.keep_alive());
  res->body() = "The resource '" + target + "' was not found.";
  res->prepare_payload();

  LOG("Send the response");
  auto self = shared_from_this();
  http::async_write(
    socket_,
    *res,
    boost::asio::bind_executor(strand_, [self, res](boost::system::error_code ec, std::size_t) {
      self->socket_.shutdown(tcp::socket::shutdown_send, ec);
    }));
}
