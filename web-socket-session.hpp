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
  void run(http::request<http::string_body> req);

private:
  void init_encoder();
  void start_sending_frames();
  int encode_and_send_frame();

  websocket::stream<tcp::socket> ws_;
  AVCodec *codec_ = nullptr;
  AVCodecContext *codec_context_ = nullptr;
  AVFrame *frame_ = nullptr;
  int frame_index_ = 0;

  // Screen capture parameters
  const int width_ = 1920;
  const int height_ = 1080;
  const int x_ = 0;
  const int y_ = 0;
};
