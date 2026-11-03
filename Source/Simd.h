#pragma once

#ifndef SYNTH2_SSE2
 #if (defined (__SSE2__) && ! defined (__FMA__)) || defined (_M_X64)
  #define SYNTH2_SSE2 1
 #else
  #define SYNTH2_SSE2 0
 #endif
#endif

#if SYNTH2_SSE2
 #include <cstdint>
 #include <emmintrin.h>

namespace Simd
{
    struct Floats
    {
        __m128 v;
        Floats() noexcept : v (_mm_setzero_ps()) {}
        Floats (__m128 value) noexcept : v (value) {}
        Floats (float value) noexcept : v (_mm_set1_ps (value)) {}
        static Floats load (const float* from) noexcept { return _mm_loadu_ps (from); }
        void store (float* to) const noexcept           { _mm_storeu_ps (to, v); }
        friend Floats operator+ (Floats a, Floats b) noexcept { return _mm_add_ps (a.v, b.v); }
        friend Floats operator- (Floats a, Floats b) noexcept { return _mm_sub_ps (a.v, b.v); }
        friend Floats operator* (Floats a, Floats b) noexcept { return _mm_mul_ps (a.v, b.v); }
    };

    struct Ints
    {
        __m128i v;
        Ints() noexcept : v (_mm_setzero_si128()) {}
        Ints (__m128i value) noexcept : v (value) {}
        Ints (std::uint32_t value) noexcept : v (_mm_set1_epi32 ((int) value)) {}
        friend Ints operator+ (Ints a, Ints b) noexcept { return _mm_add_epi32 (a.v, b.v); }
        friend Ints operator^ (Ints a, Ints b) noexcept { return _mm_xor_si128 (a.v, b.v); }
    };

    struct Doubles
    {
        __m128d v;
        Doubles (__m128d value) noexcept : v (value) {}
        Doubles (double value) noexcept : v (_mm_set1_pd (value)) {}
        double first() const noexcept  { return _mm_cvtsd_f64 (v); }
        double second() const noexcept { return _mm_cvtsd_f64 (_mm_unpackhi_pd (v, v)); }
        friend Doubles operator+ (Doubles a, Doubles b) noexcept { return _mm_add_pd (a.v, b.v); }
        friend Doubles operator- (Doubles a, Doubles b) noexcept { return _mm_sub_pd (a.v, b.v); }
        friend Doubles operator* (Doubles a, Doubles b) noexcept { return _mm_mul_pd (a.v, b.v); }
    };
}
#endif
