#pragma once
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

class Rgb2Yuv
{
public:
  Rgb2Yuv(int nThreads, int w, int h);
  ~Rgb2Yuv();
  void convert(const uint8_t *src, int srcLineSize, uint8_t *const dst[], const int dstStride[]);

private:
  void worker(int threadId);

  int width;
  int height;

  struct ThreadData
  {
    int startRow;
    int endRow;
    bool ready = false;
    std::thread thread;
  };

  std::vector<ThreadData> threadsData;

  const uint8_t *src;
  int srcLineSize;
  uint8_t *dstY;
  uint8_t *dstU;
  uint8_t *dstV;
  int dstStrideY;
  int dstStrideU;
  int dstStrideV;

  std::mutex mutex;
  std::condition_variable cvThread;
  std::condition_variable cvMain;
  bool stop;
};
