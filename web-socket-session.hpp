#pragma once
#include <X11/Xlib.h>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <log/log.hpp>
#include <memory>
#include <opus/opus.h>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <pulse/error.h>
#include <pulse/simple.h>
}

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{
public:
  WebSocketSession(tcp::socket socket);
  ~WebSocketSession();
  auto run(http::request<http::string_body> req) -> void;

private:
  auto audioThreadFunc() -> void;
  auto doRead() -> void;
  auto encodeAndSendFrame() -> int;
  auto initAudio() -> void;
  auto initEncoder() -> void;
  auto onMessage(boost::system::error_code ec, std::size_t bytes_transferred) -> void;
  auto simulateMouseEvent(const std::string &type, int x, int y) -> void;
  auto startSendingFrames() -> void;
  auto videoThreadFunc() -> void;

  websocket::stream<tcp::socket> ws;
  AVCodec *codec = nullptr;
  AVCodecContext *codecContext = nullptr;
  AVFrame *frame = nullptr;
  int frameIndex = 0;
  const int width = 1920;
  const int height = 1080;
  const int x = 0;
  const int y = 0;
  pa_simple *paStream = nullptr;
  std::atomic<bool> isRunning = true;
  std::mutex avMutex;
  OpusEncoder *opusEncoder = nullptr;
  int opusBitrate = 128'000;
  decltype(std::chrono::steady_clock::now() - std::chrono::steady_clock::now()) grabAcc;
  decltype(std::chrono::steady_clock::now() - std::chrono::steady_clock::now()) colorConvAcc;
  decltype(std::chrono::steady_clock::now() - std::chrono::steady_clock::now()) encAcc;
  int benchCnt = 0;
  Display *display = nullptr;
  boost::beast::flat_buffer buffer;
};
