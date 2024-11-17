#include "session.hpp"
#include "web-socket-session.hpp"
#include <fstream>
#include <log/log.hpp>

namespace websocket = boost::beast::websocket;

Session::Session(tcp::socket socket) : socket(std::move(socket)), strand(socket.get_executor()) {}

void Session::run()
{
  doRead();
}

void Session::doRead()
{
  LOG("Read a request");
  auto self = shared_from_this();
  http::async_read(socket,
                   buffer,
                   req,
                   boost::asio::bind_executor(
                     strand, [self](boost::system::error_code ec, std::size_t bytes_transferred) {
                       boost::ignore_unused(bytes_transferred);
                       if (!ec)
                         self->handleRequest();
                     }));
}

void Session::handleRequest()
{
  LOG("Handle WebSocket handshake");
  if (websocket::is_upgrade(req))
  {
    LOG("Create WebSocket session and transfer ownership of the socket");
    std::make_shared<WebSocketSession>(std::move(socket))->run(std::move(req));
    return;
  }

  LOG("Handle HTTP request");
  handleHttpRequest();
}

void Session::handleHttpRequest()
{
  LOG("Build the path to the requested file");
  const auto path = [&]() {
    auto r = req.target().to_string();
    if (r == "/")
      return std::string{"/index.html"};
    return r;
  }();

  const auto content = [&]() {
    const auto fullPath = "." + path; // Assuming files are in the current directory

    LOG("Open the file", fullPath);
    std::ifstream file(fullPath, std::ios::in | std::ios::binary);
    if (!file)
      return std::string{};

    LOG("Read the file content");
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
  }();

  if (content.empty())
  {
    sendNotFoundResponse(path);
    return;
  }

  LOG("Determine the content type");
  const auto contentType = [&]() {
    if (path.ends_with(".html"))
      return "text/html";
    else if (path.ends_with(".js"))
      return "application/javascript";
    else if (path.ends_with(".css"))
      return "text/css";
    else
      return "application/octet-stream";
  }();

  LOG("Build the response");
  // Use a shared_ptr to extend the lifetime of res
  auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req.version());
  res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res->set(http::field::content_type, contentType);
  res->keep_alive(req.keep_alive());
  res->body() = content;
  res->prepare_payload();

  LOG("Send the response", content);
  auto self = shared_from_this();
  http::async_write(
    socket,
    *res,
    boost::asio::bind_executor(strand, [self, res](boost::system::error_code ec, std::size_t) {
      LOG("response sent");
      self->socket.shutdown(tcp::socket::shutdown_send, ec);
    }));
}

void Session::sendNotFoundResponse(const std::string &target)
{
  LOG("File not found, send 404 response");
  auto res = std::make_shared<http::response<http::string_body>>(http::status::not_found, req.version());
  res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res->set(http::field::content_type, "text/html");
  res->keep_alive(req.keep_alive());
  res->body() = "The resource '" + target + "' was not found.";
  res->prepare_payload();

  LOG("Send the response");
  auto self = shared_from_this();
  http::async_write(
    socket,
    *res,
    boost::asio::bind_executor(strand, [self, res](boost::system::error_code ec, std::size_t) {
      self->socket.shutdown(tcp::socket::shutdown_send, ec);
    }));
}
