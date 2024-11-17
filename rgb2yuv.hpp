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
  void convert(uint8_t *src, int srcLineSize, uint8_t *const dst[], const int dstStride[]);

private:
  void worker(int threadId);

  int width;
  int height;

  struct ThreadData
  {
    std::thread thread;
    int startRow;
    int endRow;
    bool ready;
  };

  std::vector<ThreadData> threadsData;

  uint8_t *src;
  int srcLineSize;
  uint8_t *dstY;
  uint8_t *dstU;
  uint8_t *dstV;
  int dstStrideY;
  int dstStrideU;
  int dstStrideV;

  std::mutex mutex;
  std::condition_variable cv;
  bool stop;
};