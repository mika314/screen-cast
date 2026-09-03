#include "session.hpp"
#include "web-socket-session.hpp"
#include <cstdlib>
#include <log/log.hpp>
#include <string>

namespace
{
  auto printUsage(const char *argv0) -> void
  {
    LOG("Usage:", argv0, "[--gop <frames>]");
    LOG("  --gop <frames>  Keyframe interval in frames (default " + std::to_string(defaultGopSize) +
        "; at " + std::to_string(videoFps) + " fps, " + std::to_string(videoFps) + " ≈ 1s)");
  }

  auto parseArgs(int argc, char **argv) -> bool
  {
    for (int i = 1; i < argc; ++i)
    {
      const auto arg = std::string{argv[i]};
      if (arg == "--gop")
      {
        if (i + 1 >= argc)
        {
          LOG("Missing value for --gop");
          printUsage(argv[0]);
          return false;
        }
        char *end = nullptr;
        const auto value = std::strtol(argv[++i], &end, 10);
        if (end == argv[i] || *end != '\0' || value < 1)
        {
          LOG("Invalid --gop value:", argv[i], "(expected positive integer frames)");
          printUsage(argv[0]);
          return false;
        }
        gopSize = static_cast<int>(value);
      }
      else if (arg == "--help" || arg == "-h")
      {
        printUsage(argv[0]);
        std::exit(0);
      }
      else
      {
        LOG("Unknown argument:", arg);
        printUsage(argv[0]);
        return false;
      }
    }
    return true;
  }
} // namespace

void doAccept(tcp::acceptor &acceptor)
{
  acceptor.async_accept([&](boost::system::error_code ec, tcp::socket socket) {
    if (ec)
    {
      LOG("Accept failed:", ec.message());
      doAccept(acceptor);
      return;
    }
    std::make_shared<Session>(std::move(socket))->run();
    doAccept(acceptor);
  });
}

auto main(int argc, char **argv) -> int
{
  try
  {
    if (!parseArgs(argc, argv))
      return 1;

    LOG("GOP size:", gopSize, "frames (~", static_cast<double>(gopSize) / videoFps, "s at", videoFps, "fps)");

    auto ioc = boost::asio::io_context{1};
    auto endpoint = tcp::endpoint{tcp::v4(), 8090};
    auto acceptor = tcp::acceptor{ioc, endpoint};
    doAccept(acceptor);
    ioc.run();
  }
  catch (const std::exception &e)
  {
    LOG("Error:", e.what());
  }
}
