#pragma once

#include <cstdint>

// Bit-for-bit port of the JS `mulberry32` (index.html:229). Every operation
// below is unsigned 32-bit and wraps on overflow, which in C++ is
// well-defined (mod 2^32) and matches JS's `|0`/`>>>0` truncation exactly --
// multiplying two uint32_t and keeping the low 32 bits reproduces
// `Math.imul` bit-for-bit regardless of signedness. Do not "clean up" the
// operation order here: determinism depends on matching it exactly, not on
// producing an equivalent-looking formula.
class Mulberry32 {
public:
    explicit Mulberry32(uint32_t seed) : a_(seed) {}

    double next() {
        a_ = a_ + 0x6D2B79F5u;
        uint32_t t = a_ ^ (a_ >> 15);
        t = t * (1u | a_);
        uint32_t x2 = t ^ (t >> 7);
        uint32_t m = x2 * (61u | t);
        uint32_t tNew = (t + m) ^ t;
        return static_cast<double>(tNew ^ (tNew >> 14)) / 4294967296.0;
    }

private:
    uint32_t a_;
};
