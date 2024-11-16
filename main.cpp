#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <log/log.hpp>
#include <sys/ipc.h>
#include <sys/shm.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{
public:
  WebSocketSession(tcp::socket socket) : ws_(std::move(socket))
  {
    // Initialize FFmpeg encoder
    init_encoder();
  }

  void run()
  {
    // Accept the WebSocket handshake
    ws_.accept();

    // Start sending frames
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

    codec_context_->bit_rate = 3'000'000;
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

    // Initialize SWS context for color space conversion
    sws_context_ = sws_getContext(width_,
                                  height_,
                                  AV_PIX_FMT_RGB32,
                                  width_,
                                  height_,
                                  AV_PIX_FMT_YUV420P,
                                  SWS_FAST_BILINEAR,
                                  NULL,
                                  NULL,
                                  NULL);

    if (!sws_context_)
    {
      LOG("Could not initialize the conversion context");
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

      LOG("attaching");

      XShmAttach(display, &shminfo);
      LOG("attached");

      auto target = std::chrono::milliseconds(1000 / 60);
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
          // Calculate cursor position relative to the captured image
          int cursorX = cursorImage->x - self->x_;
          int cursorY = cursorImage->y - self->y_;

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

              // Get the image pixel
              uint32_t *imagePixel = (uint32_t *)(image->data + imgY * image->bytes_per_line + imgX * 4);

              // Blend the cursor pixel over the image pixel
              // Extract alpha, red, green, blue components
              uint8_t alpha = (cursorPixel >> 24) & 0xFF;
              uint8_t cr = (cursorPixel >> 16) & 0xFF;
              uint8_t cg = (cursorPixel >> 8) & 0xFF;
              uint8_t cb = cursorPixel & 0xFF;

              uint8_t ir = (*imagePixel >> 16) & 0xFF;
              uint8_t ig = (*imagePixel >> 8) & 0xFF;
              uint8_t ib = *imagePixel & 0xFF;

              // Blend the colors
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

        // Prepare source frame data
        uint8_t *src_data[4] = {(uint8_t *)image->data, NULL, NULL, NULL};
        int src_linesize[4] = {image->bytes_per_line, 0, 0, 0};

        // Convert RGB to YUV420P
        sws_scale(self->sws_context_,
                  src_data,
                  src_linesize,
                  0,
                  self->height_,
                  self->frame_->data,
                  self->frame_->linesize);
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

        const auto diff = std::chrono::steady_clock::now() - t1;

        if (diff > target)
        {
          LOG("drop", diff - target, "grab", t2 - t1, "color conv", t3 - t2, "encode", t4 - t3);
          target = std::chrono::milliseconds(1000 / 60);
        }
        else
        {
          LOG("GOOD", target - diff, "grab", t2 - t1, "color conv", t3 - t2, "encode", t4 - t3);
          std::this_thread::sleep_for(target - diff);
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
  struct SwsContext *sws_context_ = nullptr;
  int frame_index_ = 0;

  // Screen capture parameters
  const int width_ = 1920;
  const int height_ = 1080;
  const int x_ = 0;
  const int y_ = 0;
};

auto doAccept(boost::asio::io_context &ioc, std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor)
  -> void;

void do_listen(boost::asio::io_context &ioc, tcp::endpoint endpoint)
{
  auto acceptor = std::make_shared<tcp::acceptor>(ioc);
  boost::system::error_code ec;

  acceptor->open(endpoint.protocol(), ec);
  if (ec)
  {
    LOG("Failed to open acceptor:", ec.message());
    return;
  }

  acceptor->set_option(boost::asio::socket_base::reuse_address(true), ec);
  if (ec)
  {
    LOG("Failed to set reuse_address:", ec.message());
    return;
  }

  acceptor->bind(endpoint, ec);
  if (ec)
  {
    LOG("Failed to bind:", ec.message());
    return;
  }

  acceptor->listen(boost::asio::socket_base::max_listen_connections, ec);
  if (ec)
  {
    LOG("Failed to listen:", ec.message());
    return;
  }

  doAccept(ioc, acceptor);
}

auto doAccept(boost::asio::io_context &ioc, std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor)
  -> void
{
  acceptor->async_accept([acceptor, &ioc](boost::system::error_code ec, tcp::socket socket) {
    if (!ec)
    {
      std::make_shared<WebSocketSession>(std::move(socket))->run();
    }
    else
    {
      LOG("Accept failed:", ec.message());
    }
    doAccept(ioc, acceptor);
  });
}

int main()
{
  boost::asio::io_context ioc;
  tcp::endpoint endpoint(tcp::v4(), 8090);

  do_listen(ioc, endpoint);

  ioc.run();

  return 0;
}
