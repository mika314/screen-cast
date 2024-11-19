#include "rgb2yuv.hpp"
#include <algorithm>
#include <cassert>
#include <immintrin.h>

Rgb2Yuv::Rgb2Yuv(int nThreads, int w, int h) : width(w), height(h), stop(false)
{
  assert(width % 16 == 0);
  for (auto i = 0; i < nThreads; ++i)
    threadsData.emplace_back(ThreadData{.startRow = i * height / nThreads / 2 * 2,
                                        .endRow = (i + 1) * height / nThreads / 2 * 2});

  for (auto i = 0; i < nThreads; ++i)
    threadsData[i].thread = std::thread{&Rgb2Yuv::worker, this, i};
}

Rgb2Yuv::~Rgb2Yuv()
{
  {
    auto lock = std::unique_lock<std::mutex>{mutex};
    stop = true;
  }
  cvThread.notify_all();

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
  cvThread.notify_all();

  auto lock = std::unique_lock<std::mutex>{mutex};
  cvMain.wait(lock, [this] {
    return std::all_of(
      std::begin(threadsData), std::end(threadsData), [](const auto &d) { return !d.ready; });
  });
}

auto Rgb2Yuv::worker(int threadId) -> void
{
  const __m256i y_coeff_r = _mm256_set1_epi16(66);
  const __m256i y_coeff_g = _mm256_set1_epi16(129);
  const __m256i y_coeff_b = _mm256_set1_epi16(25);
  const __m256i y_const = _mm256_set1_epi16(16 * 256 + 128);
  const __m256i uv_coeff_r = _mm256_set1_epi16(-38 / 2);
  const __m256i uv_coeff_g = _mm256_set1_epi16(-74 / 2);
  const __m256i uv_coeff_b = _mm256_set1_epi16(112 / 2);
  const __m256i uv_const = _mm256_set1_epi16(128 / 2 + 128 * 128);

  for (;;)
  {
    auto lock = std::unique_lock<std::mutex>{mutex};
    cvThread.wait(lock, [threadId, this] { return threadsData[threadId].ready || stop; });

    if (stop)
      break;

    const auto startRow = threadsData[threadId].startRow;
    const auto endRow = threadsData[threadId].endRow;

    lock.unlock();

    for (auto y = startRow; y < endRow; ++y)
    {
      const auto srcLine = src + (height - y - 1) * srcLineSize;
      const auto src2Line = src + (height - y - 1 - 1) * srcLineSize;
      const auto dstYLine = dstY + y * dstStrideY;
      const auto dstULine = dstU + (y / 2) * dstStrideU;
      const auto dstVLine = dstV + (y / 2) * dstStrideV;

      for (auto x = 0; x < width; x += 16) // Process 16 pixels at a time
      {

        // Load 48 bytes (16 RGB pixels)
        const auto rgb0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&srcLine[x * 3]));
        const auto rgb1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&srcLine[x * 3 + 16]));
        const auto rgb2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&srcLine[x * 3 + 32]));

        // clang-format off
        const auto r0 = _mm_shuffle_epi8(rgb0, _mm_setr_epi8(
           0,  3,  6,  9, 12, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));
        const auto g0 = _mm_shuffle_epi8(rgb0, _mm_setr_epi8(
           1,  4,  7, 10, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));
        const auto b0 = _mm_shuffle_epi8(rgb0, _mm_setr_epi8(
           2,  5,  8, 11, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));

        const auto r1 = _mm_shuffle_epi8(rgb1, _mm_setr_epi8(
          -1, -1, -1, -1, -1, -1,  2,  5,  8, 11, 14, -1, -1, -1, -1, -1));
        const auto g1 = _mm_shuffle_epi8(rgb1, _mm_setr_epi8(
          -1, -1, -1, -1, -1,  0,  3,  6,  9, 12, 15, -1, -1, -1, -1, -1));
        const auto b1 = _mm_shuffle_epi8(rgb1, _mm_setr_epi8(
          -1, -1, -1, -1, -1,  1,  4,  7, 10, 13, -1, -1, -1, -1, -1, -1));

        const auto r2 = _mm_shuffle_epi8(rgb2, _mm_setr_epi8(
          -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  1,  4,  7, 10, 13));
        const auto g2 = _mm_shuffle_epi8(rgb2, _mm_setr_epi8(
          -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  2,  5,  8, 11, 14));
        const auto b2 = _mm_shuffle_epi8(rgb2, _mm_setr_epi8(
          -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  0,  3,  6,  9, 12, 15));
        // clang-format on

        const auto r8 = _mm_or_si128(r2, _mm_or_si128(r0, r1));
        const auto g8 = _mm_or_si128(g2, _mm_or_si128(g0, g1));
        const auto b8 = _mm_or_si128(b2, _mm_or_si128(b0, b1));

        {
          // // Combine shifted parts into final registers
          const auto r = _mm256_cvtepu8_epi16(r8);
          const auto g = _mm256_cvtepu8_epi16(g8);
          const auto b = _mm256_cvtepu8_epi16(b8);

          {
            // Compute Y = (66*r + 129*g + 25*b + 128) >> 8 + 16
            __m256i y_val = _mm256_add_epi16(
              _mm256_add_epi16(_mm256_mullo_epi16(r, y_coeff_r), _mm256_mullo_epi16(g, y_coeff_g)),
              _mm256_mullo_epi16(b, y_coeff_b));
            y_val = _mm256_add_epi16(y_val, y_const);

            // clang-format off
            auto y_packed = _mm256_shuffle_epi8(y_val, _mm256_setr_epi8(
              1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1,
              1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
            // clang-format on

            // Store the packed 128-bit register
            _mm_storeu_si128(
              reinterpret_cast<__m128i *>(&dstYLine[x]),
              _mm256_castsi256_si128(_mm256_permute4x64_epi64(y_packed, _MM_SHUFFLE(2, 0, 2, 0))));
          }
        }

        if (y % 2 == 0)
        {
          // Split r, g, and b into odd and even components
          const auto r_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            r8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto r_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            r8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto g_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            g8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto g_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            g8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto b_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            b8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto b_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            b8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));

          const auto rgbOdd0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src2Line[x * 3]));
          const auto rgbOdd1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src2Line[x * 3 + 16]));
          const auto rgbOdd2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src2Line[x * 3 + 32]));

          // clang-format off
          const auto rOdd0 = _mm_shuffle_epi8(rgbOdd0, _mm_setr_epi8(
             0,  3,  6,  9, 12, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));
          const auto gOdd0 = _mm_shuffle_epi8(rgbOdd0, _mm_setr_epi8(
             1,  4,  7, 10, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));
          const auto bOdd0 = _mm_shuffle_epi8(rgbOdd0, _mm_setr_epi8(
             2,  5,  8, 11, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));

          const auto rOdd1 = _mm_shuffle_epi8(rgbOdd1, _mm_setr_epi8(
            -1, -1, -1, -1, -1, -1,  2,  5,  8, 11, 14, -1, -1, -1, -1, -1));
          const auto gOdd1 = _mm_shuffle_epi8(rgbOdd1, _mm_setr_epi8(
            -1, -1, -1, -1, -1,  0,  3,  6,  9, 12, 15, -1, -1, -1, -1, -1));
          const auto bOdd1 = _mm_shuffle_epi8(rgbOdd1, _mm_setr_epi8(
            -1, -1, -1, -1, -1,  1,  4,  7, 10, 13, -1, -1, -1, -1, -1, -1));

          const auto rOdd2 = _mm_shuffle_epi8(rgbOdd2, _mm_setr_epi8(
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  1,  4,  7, 10, 13));
          const auto gOdd2 = _mm_shuffle_epi8(rgbOdd2, _mm_setr_epi8(
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  2,  5,  8, 11, 14));
          const auto bOdd2 = _mm_shuffle_epi8(rgbOdd2, _mm_setr_epi8(
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  0,  3,  6,  9, 12, 15));
          // clang-format on

          const auto rOdd8 = _mm_or_si128(rOdd2, _mm_or_si128(rOdd0, rOdd1));
          const auto gOdd8 = _mm_or_si128(gOdd2, _mm_or_si128(gOdd0, gOdd1));
          const auto bOdd8 = _mm_or_si128(bOdd2, _mm_or_si128(bOdd0, bOdd1));

          const auto rOdd_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            rOdd8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto rOdd_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            rOdd8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto gOdd_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            gOdd8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto gOdd_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            gOdd8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto bOdd_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            bOdd8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const auto bOdd_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            bOdd8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));

          // Compute averages for r, g, and b
          const auto r_ave = _mm256_srli_epi16(
            _mm256_add_epi16(_mm256_add_epi16(r_odd, r_even), _mm256_add_epi16(rOdd_odd, rOdd_even)), 2);
          const auto g_ave = _mm256_srli_epi16(
            _mm256_add_epi16(_mm256_add_epi16(g_odd, g_even), _mm256_add_epi16(gOdd_odd, gOdd_even)), 2);
          const auto b_ave = _mm256_srli_epi16(
            _mm256_add_epi16(_mm256_add_epi16(b_odd, b_even), _mm256_add_epi16(bOdd_odd, bOdd_even)), 2);

          // Compute U
          auto u_val = _mm256_add_epi16(_mm256_add_epi16(_mm256_mullo_epi16(r_ave, uv_coeff_r),
                                                         _mm256_mullo_epi16(g_ave, uv_coeff_g)),
                                        _mm256_mullo_epi16(b_ave, uv_coeff_b));
          u_val = _mm256_add_epi16(u_val, uv_const);

          // Compute V
          auto v_val = _mm256_add_epi16(_mm256_add_epi16(_mm256_mullo_epi16(r_ave, uv_coeff_b),
                                                         _mm256_mullo_epi16(g_ave, uv_coeff_g)),
                                        _mm256_mullo_epi16(b_ave, uv_coeff_r));
          v_val = _mm256_add_epi16(v_val, uv_const);

          u_val = _mm256_mullo_epi16(u_val, _mm256_set1_epi16(2));
          v_val = _mm256_mullo_epi16(v_val, _mm256_set1_epi16(2));

          // clang-format off
          // Pack and store U and V
          auto u_packed = _mm256_shuffle_epi8(u_val, _mm256_setr_epi8(
            1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1,
            1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
          auto v_packed = _mm256_shuffle_epi8(v_val, _mm256_setr_epi8(
            1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1,
            1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
          // clang-format on

          u_packed = _mm256_permute4x64_epi64(u_packed, _MM_SHUFFLE(2, 0, 2, 0));
          v_packed = _mm256_permute4x64_epi64(v_packed, _MM_SHUFFLE(2, 0, 2, 0));

          _mm_storel_epi64(reinterpret_cast<__m128i *>(&dstULine[x / 2]),
                           _mm256_castsi256_si128(u_packed));
          _mm_storel_epi64(reinterpret_cast<__m128i *>(&dstVLine[x / 2]),
                           _mm256_castsi256_si128(v_packed));
        }
      }
    }

    lock.lock();
    threadsData[threadId].ready = false;
    cvMain.notify_one();
  }
}
