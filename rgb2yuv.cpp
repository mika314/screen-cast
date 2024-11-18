#include "rgb2yuv.hpp"
#include <algorithm>

Rgb2Yuv::Rgb2Yuv(int nThreads, int w, int h) : width(w), height(h), stop(false)
{
  for (auto i = 0; i < nThreads; ++i)
    threadsData.emplace_back(
      ThreadData{.startRow = i * height / nThreads, .endRow = (i + 1) * height / nThreads});

  for (auto i = 0; i < nThreads; ++i)
    threadsData[i].thread = std::thread{&Rgb2Yuv::worker, this, i};
}

Rgb2Yuv::~Rgb2Yuv()
{
  {
    auto lock = std::unique_lock<std::mutex>{mutex};
    stop = true;
  }
  cv.notify_all();

  for (auto &t : threadsData)
    if (t.thread.joinable())
      t.thread.join();
}

auto Rgb2Yuv::convert(const uint8_t *aSrc, int aSrcLineSize, uint8_t *const dst[], const int dstStride[])
  -> void
{
  {
    auto lock = std::unique_lock<std::mutex>{mutex};
    src = aSrc;
    srcLineSize = aSrcLineSize;
    dstY = dst[0];
    dstU = dst[1];
    dstV = dst[2];
    dstStrideY = dstStride[0];
    dstStrideU = dstStride[1];
    dstStrideV = dstStride[2];
    for (auto &d : threadsData)
      d.ready = true;
  }
  cv.notify_all();

  auto lock = std::unique_lock<std::mutex>{mutex};
  cv.wait(lock, [this] {
    return std::all_of(
      std::begin(threadsData), std::begin(threadsData), [](const auto &d) { return !d.ready; });
  });
}

auto Rgb2Yuv::worker(int threadId) -> void
{
  for (;;)
  {
    auto lock = std::unique_lock<std::mutex>{mutex};
    cv.wait(lock, [threadId, this] { return threadsData[threadId].ready || stop; });

    if (stop)
      break;

    const auto startRow = threadsData[threadId].startRow;
    const auto endRow = threadsData[threadId].endRow;

    lock.unlock();

    for (auto y = startRow; y < endRow; ++y)
    {
      const auto srcLine = src + y * srcLineSize;
      auto dstYLine = dstY + y * dstStrideY;

      for (auto x = 0; x < width; ++x)
      {
        const auto r = srcLine[x * 4 + 2];
        const auto g = srcLine[x * 4 + 1];
        const auto b = srcLine[x * 4 + 0];

        dstYLine[x] = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
      }

      if (y % 2 == 0)
      {
        auto dstULine = dstU + (y / 2) * dstStrideU;
        auto dstVLine = dstV + (y / 2) * dstStrideV;

        for (auto x = 0; x < width; x += 2)
        {
          const auto idx0 = x * 4;
          const auto idx1 = (x + 1) * 4;
          const auto idx2 = (y + 1 < height) ? (x * 4 + srcLineSize) : idx0;
          const auto idx3 = (y + 1 < height) ? ((x + 1) * 4 + srcLineSize) : idx1;

          const auto r =
            (srcLine[idx0 + 2] + srcLine[idx1 + 2] + srcLine[idx2 + 2] + srcLine[idx3 + 2]) / 4;
          const auto g =
            (srcLine[idx0 + 1] + srcLine[idx1 + 1] + srcLine[idx2 + 1] + srcLine[idx3 + 1]) / 4;
          const auto b =
            (srcLine[idx0 + 0] + srcLine[idx1 + 0] + srcLine[idx2 + 0] + srcLine[idx3 + 0]) / 4;

          const auto uValue = static_cast<uint8_t>(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
          const auto vValue = static_cast<uint8_t>(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);

          dstULine[x / 2] = uValue;
          dstVLine[x / 2] = vValue;
        }
      }
    }

    lock.lock();
    threadsData[threadId].ready = false;
    cv.notify_one();
  }
}
