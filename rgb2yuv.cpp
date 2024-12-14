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

// Convert RGB to NV12:
//
//  dst[0] = Y-plane  (height rows, each row 'dstStride[0]' bytes)
//  dst[1] = UV-plane (height/2 rows, each row 'dstStride[1]' bytes, 2 bytes per 2×2 block: U then V)
auto Rgb2Yuv::convert(const uint8_t *aSrc, int aSrcLineSize, uint8_t *const dst[], const int dstStride[])
  -> void
{
  {
    auto lock = std::unique_lock<std::mutex>{mutex};
    src = aSrc;
    srcLineSize = aSrcLineSize;
    // For NV12:
    dstY = dst[0];  // Y plane
    dstUV = dst[1]; // Interleaved UV plane
    dstStrideY = dstStride[0];
    dstStrideUV = dstStride[1];

    // Mark each thread as ready
    for (auto &d : threadsData)
      d.ready = true;
  }
  cvThread.notify_all();

  // Wait until all threads finish
  auto lock = std::unique_lock<std::mutex>{mutex};
  cvMain.wait(lock, [this] {
    return std::all_of(
      std::begin(threadsData), std::end(threadsData), [](const auto &d) { return !d.ready; });
  });
}

auto Rgb2Yuv::worker(int threadId) -> void
{
  // YUV coefficients (8-bit -> 16-bit expansions used by AVX2).
  const __m256i y_coeff_r = _mm256_set1_epi16(66);
  const __m256i y_coeff_g = _mm256_set1_epi16(129);
  const __m256i y_coeff_b = _mm256_set1_epi16(25);
  const __m256i y_const = _mm256_set1_epi16(16 * 256 + 128); // adds rounding + offset for Y

  // For NV12, we still compute the same U and V mathematically, but store them interleaved.
  // Usually the coefficients for U = -38r -74g +112b, V = 112r -94g -18b, etc.
  // The code below approximates that in half or partial steps.
  const __m256i uv_coeff_r = _mm256_set1_epi16(-38 / 2); // for downsample averaging
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
      // Because the source is read from bottom to top, note the indexing:
      const auto srcLine = src + (height - y - 1) * srcLineSize;
      const auto src2Line = src + (height - (y + 1) - 1) * srcLineSize; // next row for chroma
      const auto dstYLine = dstY + y * dstStrideY;

      // For NV12, the UV-plane row index is y/2
      // Each line in the UV plane has 'width' bytes (since for each pair of pixels, we store 2 bytes: U
      // and V).
      const bool doChroma = (y % 2 == 0); // Only compute/store UV on even rows
      uint8_t *dstUVLine = nullptr;
      if (doChroma)
        dstUVLine = dstUV + (y / 2) * dstStrideUV;

      for (int x = 0; x < width; x += 16) // Process 16 RGB pixels at a time
      {
        //
        // 1) Load 16 RGB pixels (48 bytes) from the *current row*
        //
        const __m128i rgb0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&srcLine[x * 3 + 0]));
        const __m128i rgb1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&srcLine[x * 3 + 16]));
        const __m128i rgb2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&srcLine[x * 3 + 32]));

        // Shuffle to isolate R, G, B (16 bytes in total, but scattered).
        // clang-format off
        const __m128i r0 = _mm_shuffle_epi8(rgb0, _mm_setr_epi8(
          0,  3,  6,  9, 12, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));
        const __m128i g0 = _mm_shuffle_epi8(rgb0, _mm_setr_epi8(
          1,  4,  7, 10, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));
        const __m128i b0 = _mm_shuffle_epi8(rgb0, _mm_setr_epi8(
          2,  5,  8, 11, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1));

        const __m128i r1 = _mm_shuffle_epi8(rgb1, _mm_setr_epi8(
          -1, -1, -1, -1, -1, -1,  2,  5,  8, 11, 14, -1, -1, -1, -1, -1));
        const __m128i g1 = _mm_shuffle_epi8(rgb1, _mm_setr_epi8(
          -1, -1, -1, -1, -1,  0,  3,  6,  9, 12, 15, -1, -1, -1, -1, -1));
        const __m128i b1 = _mm_shuffle_epi8(rgb1, _mm_setr_epi8(
          -1, -1, -1, -1, -1,  1,  4,  7, 10, 13, -1, -1, -1, -1, -1, -1));

        const __m128i r2 = _mm_shuffle_epi8(rgb2, _mm_setr_epi8(
          -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  1,  4,  7, 10, 13));
        const __m128i g2 = _mm_shuffle_epi8(rgb2, _mm_setr_epi8(
          -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  2,  5,  8, 11, 14));
        const __m128i b2 = _mm_shuffle_epi8(rgb2, _mm_setr_epi8(
          -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  0,  3,  6,  9, 12, 15));
        // clang-format on

        const __m128i r8 = _mm_or_si128(r2, _mm_or_si128(r0, r1));
        const __m128i g8 = _mm_or_si128(g2, _mm_or_si128(g0, g1));
        const __m128i b8 = _mm_or_si128(b2, _mm_or_si128(b0, b1));

        // Convert to 16-bit
        const __m256i r16 = _mm256_cvtepu8_epi16(r8);
        const __m256i g16 = _mm256_cvtepu8_epi16(g8);
        const __m256i b16 = _mm256_cvtepu8_epi16(b8);

        //
        // 2) Compute Y
        //
        {
          __m256i y_val = _mm256_add_epi16(
            _mm256_add_epi16(_mm256_mullo_epi16(r16, y_coeff_r), _mm256_mullo_epi16(g16, y_coeff_g)),
            _mm256_mullo_epi16(b16, y_coeff_b));
          y_val = _mm256_add_epi16(y_val, y_const); // add offset+rounding

          // Now we want to pack 16 16-bit values into 16 8-bit Y values
          // clang-format off
          __m256i y_packed = _mm256_shuffle_epi8(
              y_val,
              _mm256_setr_epi8(
                1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1,
                1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
          // clang-format on

          // We only need the lower 128 bits twice. So reorder (2,0,2,0) merges them.
          y_packed = _mm256_permute4x64_epi64(y_packed, _MM_SHUFFLE(2, 0, 2, 0));
          // Store 16 bytes of Y
          _mm_storeu_si128(reinterpret_cast<__m128i *>(&dstYLine[x]), _mm256_castsi256_si128(y_packed));
        }

        //
        // 3) Compute and store interleaved UV only on even rows
        //
        if (doChroma)
        {
          // We'll also need the row below (src2Line) to average the chroma.
          // Each pixel in the subsampled plane corresponds to a 2×2 block in RGB.

          // First, split current row’s R/G/B into odd/even sets
          const __m256i r_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            r8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const __m256i r_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            r8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));

          const __m256i g_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            g8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const __m256i g_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            g8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));

          const __m256i b_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            b8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const __m256i b_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            b8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));

          // Now load the row below for the other half of the 2×2 block
          const __m128i rgbOdd0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src2Line[x * 3 + 0]));
          const __m128i rgbOdd1 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src2Line[x * 3 + 16]));
          const __m128i rgbOdd2 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src2Line[x * 3 + 32]));

          // clang-format off
          const __m128i rOdd0 = _mm_shuffle_epi8(rgbOdd0, _mm_setr_epi8(
            0, 3, 6,  9, 12, 15, -1,-1, -1,-1,-1,-1, -1,-1,-1,-1));
          const __m128i gOdd0 = _mm_shuffle_epi8(rgbOdd0, _mm_setr_epi8(
            1, 4, 7, 10, 13, -1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1));
          const __m128i bOdd0 = _mm_shuffle_epi8(rgbOdd0, _mm_setr_epi8(
            2, 5, 8, 11, 14, -1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1));

          const __m128i rOdd1 = _mm_shuffle_epi8(rgbOdd1, _mm_setr_epi8(
            -1,-1,-1,-1, -1,-1,  2, 5,  8,11,14, -1,-1,-1,-1,-1));
          const __m128i gOdd1 = _mm_shuffle_epi8(rgbOdd1, _mm_setr_epi8(
            -1,-1,-1,-1, -1, 0,  3, 6,  9,12,15, -1,-1,-1,-1,-1));
          const __m128i bOdd1 = _mm_shuffle_epi8(rgbOdd1, _mm_setr_epi8(
            -1,-1,-1,-1, -1, 1,  4, 7, 10,13, -1,-1,-1,-1,-1,-1));

          const __m128i rOdd2 = _mm_shuffle_epi8(rgbOdd2, _mm_setr_epi8(
            -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,  1,  4,  7, 10,13));
          const __m128i gOdd2 = _mm_shuffle_epi8(rgbOdd2, _mm_setr_epi8(
            -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,  2,  5,  8, 11,14));
          const __m128i bOdd2 = _mm_shuffle_epi8(rgbOdd2, _mm_setr_epi8(
            -1,-1,-1,-1, -1,-1,-1,-1, -1,-1, 0,  3,  6,  9, 12,15));
          // clang-format on

          const __m128i rOdd8 = _mm_or_si128(rOdd2, _mm_or_si128(rOdd0, rOdd1));
          const __m128i gOdd8 = _mm_or_si128(gOdd2, _mm_or_si128(gOdd0, gOdd1));
          const __m128i bOdd8 = _mm_or_si128(bOdd2, _mm_or_si128(bOdd0, bOdd1));

          const __m256i rOdd_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            rOdd8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const __m256i rOdd_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            rOdd8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));
          const __m256i gOdd_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            gOdd8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const __m256i gOdd_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            gOdd8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));
          const __m256i bOdd_odd = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            bOdd8, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1)));
          const __m256i bOdd_even = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(
            bOdd8, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1)));

          // Average the 2×2 block in each channel
          const __m256i r_ave = _mm256_srli_epi16(
            _mm256_add_epi16(_mm256_add_epi16(r_odd, r_even), _mm256_add_epi16(rOdd_odd, rOdd_even)), 2);
          const __m256i g_ave = _mm256_srli_epi16(
            _mm256_add_epi16(_mm256_add_epi16(g_odd, g_even), _mm256_add_epi16(gOdd_odd, gOdd_even)), 2);
          const __m256i b_ave = _mm256_srli_epi16(
            _mm256_add_epi16(_mm256_add_epi16(b_odd, b_even), _mm256_add_epi16(bOdd_odd, bOdd_even)), 2);

          // Compute U
          __m256i u_val = _mm256_add_epi16(_mm256_add_epi16(_mm256_mullo_epi16(r_ave, uv_coeff_r),
                                                            _mm256_mullo_epi16(g_ave, uv_coeff_g)),
                                           _mm256_mullo_epi16(b_ave, uv_coeff_b));
          u_val = _mm256_add_epi16(u_val, uv_const);

          // Compute V
          __m256i v_val = _mm256_add_epi16(
            // typical formula for V is: (112*r -94*g -18*b), etc.
            // but here we reuse uv_coeff_b, uv_coeff_g, uv_coeff_r in reversed roles
            _mm256_add_epi16(_mm256_mullo_epi16(r_ave, uv_coeff_b),
                             _mm256_mullo_epi16(g_ave, uv_coeff_g)),
            _mm256_mullo_epi16(b_ave, uv_coeff_r));
          v_val = _mm256_add_epi16(v_val, uv_const);

          // The code as given tries to multiply by 2 again.
          // This step depends on exactly how the partial coefficients were set.
          u_val = _mm256_mullo_epi16(u_val, _mm256_set1_epi16(2));
          v_val = _mm256_mullo_epi16(v_val, _mm256_set1_epi16(2));

          // Pack down to 8-bit
          // clang-format off
          __m256i u_packed = _mm256_shuffle_epi8(u_val, _mm256_setr_epi8(
            1,3,5,7,9,11,13,15, -1,-1,-1,-1,-1,-1,-1,-1,
            1,3,5,7,9,11,13,15, -1,-1,-1,-1,-1,-1,-1,-1));
          __m256i v_packed = _mm256_shuffle_epi8(v_val, _mm256_setr_epi8(
            1,3,5,7,9,11,13,15, -1,-1,-1,-1,-1,-1,-1,-1,
            1,3,5,7,9,11,13,15, -1,-1,-1,-1,-1,-1,-1,-1));
          // clang-format on

          u_packed = _mm256_permute4x64_epi64(u_packed, _MM_SHUFFLE(2, 0, 2, 0));
          v_packed = _mm256_permute4x64_epi64(v_packed, _MM_SHUFFLE(2, 0, 2, 0));

          __m128i u_128 = _mm256_castsi256_si128(u_packed);
          __m128i v_128 = _mm256_castsi256_si128(v_packed);

          // Interleave U and V into NV12 layout: U0, V0, U1, V1, ...
          __m128i uv_interleaved = _mm_unpacklo_epi8(u_128, v_128);

          // Store 16 bytes (8 U/V pairs). This covers x..x+15 in Y, but x/2..(x/2 + 7) in UV plane.
          _mm_storeu_si128(reinterpret_cast<__m128i *>(&dstUVLine[x]), uv_interleaved);
        }
      } // x loop
    }   // y loop

    lock.lock();
    threadsData[threadId].ready = false;
    cvMain.notify_one();
  }
}
