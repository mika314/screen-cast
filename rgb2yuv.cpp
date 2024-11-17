#include "rgb2yuv.hpp"
#include <algorithm>

Rgb2Yuv::Rgb2Yuv(int nThreads, int w, int h) : width(w), height(h), stop(false)
{
  for (int i = 0; i < nThreads; ++i)
  {
    auto &threadData = threadsData.emplace_back();
    threadData.startRow = i * height / nThreads;
    threadData.endRow = (i + 1) * height / nThreads;
    threadData.thread = std::thread{&Rgb2Yuv::worker, this, i};
    threadData.ready = false;
  }
}

Rgb2Yuv::~Rgb2Yuv()
{
  {
    std::unique_lock<std::mutex> lock(mutex);
    stop = true;
  }
  cv.notify_all();

  for (auto &t : threadsData)
  {
    if (t.thread.joinable())
      t.thread.join();
  }
}

void Rgb2Yuv::convert(uint8_t *aSrc, int aSrcLineSize, uint8_t *const dst[], const int dstStride[])
{
  {
    std::unique_lock<std::mutex> lock(mutex);
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

  // Wait for all threads to finish
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [this] {
    return std::all_of(
      std::begin(threadsData), std::begin(threadsData), [](const auto &d) { return !d.ready; });
  });
}

void Rgb2Yuv::worker(int threadId)
{
  while (true)
  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [threadId, this] { return threadsData[threadId].ready || stop; });

    if (stop)
      break;

    int startRow = threadsData[threadId].startRow;
    int endRow = threadsData[threadId].endRow;

    lock.unlock();

    // Perform conversion on assigned rows
    for (int y = startRow; y < endRow; ++y)
    {
      uint8_t *srcLine = src + y * srcLineSize;
      uint8_t *dstYLine = dstY + y * dstStrideY;

      for (int x = 0; x < width; ++x)
      {
        uint8_t r = srcLine[x * 4 + 2];
        uint8_t g = srcLine[x * 4 + 1];
        uint8_t b = srcLine[x * 4 + 0];

        // Convert to Y
        uint8_t yValue = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
        dstYLine[x] = yValue;
      }

      // For U and V (every other row)
      if (y % 2 == 0)
      {
        uint8_t *dstULine = dstU + (y / 2) * dstStrideU;
        uint8_t *dstVLine = dstV + (y / 2) * dstStrideV;

        for (int x = 0; x < width; x += 2)
        {
          int idx0 = x * 4;
          int idx1 = (x + 1) * 4;
          int idx2 = (y + 1 < height) ? (x * 4 + srcLineSize) : idx0;
          int idx3 = (y + 1 < height) ? ((x + 1) * 4 + srcLineSize) : idx1;

          // Average RGB values over 2x2 block
          int r = srcLine[idx0 + 2] + srcLine[idx1 + 2] + srcLine[idx2 + 2] + srcLine[idx3 + 2];
          int g = srcLine[idx0 + 1] + srcLine[idx1 + 1] + srcLine[idx2 + 1] + srcLine[idx3 + 1];
          int b = srcLine[idx0 + 0] + srcLine[idx1 + 0] + srcLine[idx2 + 0] + srcLine[idx3 + 0];

          r /= 4;
          g /= 4;
          b /= 4;

          // Convert to U and V
          uint8_t uValue = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
          uint8_t vValue = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

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
