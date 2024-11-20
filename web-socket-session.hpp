#pragma once
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
  auto initEncoder() -> void;
  auto initAudio() -> void;
  auto startSendingFrames() -> void;
  auto encodeAndSendFrame() -> int;
  auto videoThreadFunc() -> void;
  auto audioThreadFunc() -> void;

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
};
