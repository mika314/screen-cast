#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <log/log.hpp>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{
public:
  WebSocketSession(tcp::socket socket);
  auto run(http::request<http::string_body> req) -> void;

private:
  auto initEncoder() -> void;
  auto startSendingFrames() -> void;
  auto encodeAndSendFrame() -> int;
  auto threadFunc() -> void;

  websocket::stream<tcp::socket> ws;
  AVCodec *codec = nullptr;
  AVCodecContext *codecContext = nullptr;
  AVFrame *frame = nullptr;
  int frameIndex = 0;

  const int width = 1920;
  const int height = 1080;
  const int x = 0;
  const int y = 0;
};
