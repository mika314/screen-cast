#include "rgb2yuv.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <log/log.hpp>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <thread>
#include <unistd.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
namespace http = boost::beast::http;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{
public:
  WebSocketSession(tcp::socket socket) : ws_(std::move(socket))
  {
    LOG("Initialize FFmpeg encoder");
    init_encoder();
  }

  void run(http::request<http::string_body> req)
  {
    LOG("Accept the WebSocket handshake");
    ws_.accept(req);

    LOG("Start sending frames");
    start_sending_frames();
  }

private:
  void init_encoder()
  {
    // codec_ = avcodec_find_encoder_by_name("h264_nvenc");
    codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec_)
    {
      LOG("Codec not found");
      exit(1);
    }

    codec_context_ = avcodec_alloc_context3(codec_);
    if (!codec_context_)
    {
      LOG("Could not allocate video codec context");
      exit(1);
    }

    codec_context_->bit_rate = 6'000'000;
    codec_context_->width = width_;
    codec_context_->height = height_;
    codec_context_->time_base = {1, 60};
    codec_context_->framerate = {60, 1};
    codec_context_->gop_size = 120;
    codec_context_->max_b_frames = 0; // No B-frames
    codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;

    // Low-latency settings
    codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codec_context_->thread_count = 0; // Enable multi-threading

    // H.264 specific settings
    av_opt_set(codec_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_context_->priv_data, "profile", "baseline", 0);
    av_opt_set(codec_context_->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(codec_context_, codec_, NULL) < 0)
    {
      LOG("Could not open codec");
      exit(1);
    }

    // Initialize frame
    frame_ = av_frame_alloc();
    if (!frame_)
    {
      LOG("Could not allocate video frame");
      exit(1);
    }
    frame_->format = codec_context_->pix_fmt;
    frame_->width = codec_context_->width;
    frame_->height = codec_context_->height;

    int ret = av_frame_get_buffer(frame_, 32);
    if (ret < 0)
    {
      LOG("Could not allocate the video frame data");
      exit(1);
    }
  }

  void start_sending_frames()
  {
    // Start a new thread for frame capturing and sending
    std::thread([self = shared_from_this()]() {
      Display *display = XOpenDisplay(NULL);
      if (!display)
      {
        LOG("Cannot open display");
        return;
      }
      Window root = DefaultRootWindow(display);
      // Initialize XFixes
      int event_base, error_base;
      if (!XFixesQueryExtension(display, &event_base, &error_base))
      {
        LOG("XFixes extension not available");
        return;
      }

      if (!XShmQueryExtension(display))
      {
        LOG("XShm extension not available");
        return;
      }
      XShmSegmentInfo shminfo;
      XImage *image = XShmCreateImage(display,
                                      DefaultVisual(display, DefaultScreen(display)),
                                      DefaultDepth(display, DefaultScreen(display)),
                                      ZPixmap,
                                      NULL,
                                      &shminfo,
                                      self->width_,
                                      self->height_);

      shminfo.shmid = shmget(IPC_PRIVATE, image->bytes_per_line * image->height, IPC_CREAT | 0777);
      shminfo.shmaddr = image->data = (char *)shmat(shminfo.shmid, NULL, 0);
      shminfo.readOnly = False;

      XShmAttach(display, &shminfo);

      auto rgb2yuv = Rgb2Yuv{16, self->width_, self->height_};

      auto target = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000 / 60);
      while (true)
      {
        const auto t1 = std::chrono::steady_clock::now();
        Status s = XShmGetImage(display, root, image, self->x_, self->y_, AllPlanes);
        if (!s)
        {
          LOG("Failed to get image");
          break;
        }

        XFixesCursorImage *cursorImage = XFixesGetCursorImage(display);
        if (cursorImage)
        {
          // Calculate cursor position relative to the captured image, adjusting for hotspot
          int cursorX = cursorImage->x - cursorImage->xhot - self->x_;
          int cursorY = cursorImage->y - cursorImage->yhot - self->y_;

          // Overlay the cursor image onto the captured image
          for (int j = 0; j < cursorImage->height; ++j)
          {
            int imgY = cursorY + j;
            if (imgY < 0 || imgY >= self->height_)
              continue;

            for (int i = 0; i < cursorImage->width; ++i)
            {
              int imgX = cursorX + i;
              if (imgX < 0 || imgX >= self->width_)
                continue;

              // Get the cursor pixel
              uint32_t cursorPixel = cursorImage->pixels[j * cursorImage->width + i];

              // Extract alpha, red, green, blue components
              uint8_t alpha = (cursorPixel >> 24) & 0xFF;

              // Skip fully transparent pixels to optimize
              if (alpha == 0)
                continue;

              uint8_t cr = (cursorPixel >> 16) & 0xFF;
              uint8_t cg = (cursorPixel >> 8) & 0xFF;
              uint8_t cb = cursorPixel & 0xFF;

              // Get the image pixel
              uint32_t *imagePixel = (uint32_t *)(image->data + imgY * image->bytes_per_line + imgX * 4);

              uint8_t ir = (*imagePixel >> 16) & 0xFF;
              uint8_t ig = (*imagePixel >> 8) & 0xFF;
              uint8_t ib = *imagePixel & 0xFF;

              // Blend the colors using alpha blending
              uint8_t nr = (cr * alpha + ir * (255 - alpha)) / 255;
              uint8_t ng = (cg * alpha + ig * (255 - alpha)) / 255;
              uint8_t nb = (cb * alpha + ib * (255 - alpha)) / 255;

              // Set the new pixel value
              *imagePixel = (nr << 16) | (ng << 8) | nb;
            }
          }
          // Free the cursor image
          XFree(cursorImage);
        }

        const auto t2 = std::chrono::steady_clock::now();

        // Prepare source and destination pointers
        uint8_t *src = (uint8_t *)image->data;
        int srcLineSize = image->bytes_per_line;

        uint8_t *dst[3] = {self->frame_->data[0], self->frame_->data[1], self->frame_->data[2]};
        int dstStride[3] = {
          self->frame_->linesize[0], self->frame_->linesize[1], self->frame_->linesize[2]};

        // Perform conversion
        rgb2yuv.convert(src, srcLineSize, dst, dstStride);

        const auto t3 = std::chrono::steady_clock::now();

        self->frame_->pts = self->frame_index_++;

        // Encode frame and send
        if (self->encode_and_send_frame() < 0)
        {
          LOG("Error encoding and sending frame");
          XDestroyImage(image);
          break;
        }
        const auto t4 = std::chrono::steady_clock::now();

        if (t4 > target)
        {
          LOG("drop", t4 - target, "grab", t2 - t1, "color conv", t3 - t2, "encode", t4 - t3);
          target = t4 + std::chrono::milliseconds(1000 / 60);
        }
        else
        {
          // LOG("GOOD", target - diff, "grab", t2 - t1, "color conv", t3 - t2, "encode", t4 - t3);
          std::this_thread::sleep_for(target - t4);
          target += std::chrono::milliseconds(1000 / 60);
        }
      }

      XShmDetach(display, &shminfo);
      shmdt(shminfo.shmaddr);
      shmctl(shminfo.shmid, IPC_RMID, 0);
      XCloseDisplay(display);
    }).detach();
  }

  int encode_and_send_frame()
  {
    int ret = avcodec_send_frame(codec_context_, frame_);
    if (ret < 0)
    {
      LOG("Error sending a frame for encoding");
      return ret;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
    {
      LOG("Could not allocate AVPacket");
      return -1;
    }

    while (ret >= 0)
    {
      ret = avcodec_receive_packet(codec_context_, pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      {
        av_packet_free(&pkt);
        return 0;
      }
      else if (ret < 0)
      {
        LOG("Error during encoding");
        av_packet_free(&pkt);
        return ret;
      }

      // Send packet over WebSocket
      try
      {
        ws_.binary(true);
        ws_.write(boost::asio::buffer(pkt->data, pkt->size));
      }
      catch (const std::exception &e)
      {
        LOG("WebSocket write error:", e.what());
        av_packet_unref(pkt);
        av_packet_free(&pkt);
        return -1;
      }

      av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    return 0;
  }

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

// Forward declaration
void do_accept(tcp::acceptor &acceptor);

// Session class to handle both HTTP and WebSocket sessions
class Session : public std::enable_shared_from_this<Session>
{
public:
  Session(tcp::socket socket) : socket_(std::move(socket)), strand_(socket_.get_executor()) {}

  void run()
  {
    LOG("Read a request");
    do_read();
  }

private:
  tcp::socket socket_;
  boost::asio::strand<boost::asio::any_io_executor> strand_;
  boost::beast::flat_buffer buffer_;
  http::request<http::string_body> req_;

  void do_read()
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

  void handle_request()
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

  void handle_http_request()
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

  void send_not_found_response(const std::string &target)
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
};

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
