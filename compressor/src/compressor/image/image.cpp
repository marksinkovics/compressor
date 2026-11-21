#include <compressor/image/image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <future>
#include <istream>
#include <limits>
#include <numbers>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <compressor/checksum/crc32.h>
#include <compressor/error.h>
#include <compressor/thread/pool.h>

namespace compressor
{

namespace
{

// ---------------------------------------------------------------------------------------------
// PNM

// The next header token, skipping whitespace and comments; empty at the end.
std::string pnm_token(std::istream& in)
{
    std::string token;
    int c = in.get();
    while (c != std::istream::traits_type::eof())
    {
        if (c == '#')
        {
            while (c != '\n' && c != std::istream::traits_type::eof())
            {
                c = in.get();
            }
        }
        else if (std::isspace(c) == 0)
        {
            break;
        }
        c = in.get();
    }
    while (c != std::istream::traits_type::eof() && std::isspace(c) == 0)
    {
        token.push_back(static_cast<char>(c));
        c = in.get();
    }
    return token;
}

std::optional<uint32_t> pnm_number(const std::string& token)
{
    if (token.empty() || token.size() > 9 || !std::ranges::all_of(token, [](char c) {
        return c >= '0' && c <= '9';
    }))
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(std::stoul(token));
}

// ---------------------------------------------------------------------------------------------
// Probabilities: 12 bits of a 1; the logistic domain is -2047..2047 with 8 fractional bits.

// v >> shift rounding down, also for negative v, without signed bit operations.
int64_t shift_down(int64_t v, unsigned shift)
{
    constexpr uint64_t bias = uint64_t{1} << 62U;
    return static_cast<int64_t>(((static_cast<uint64_t>(v) + bias) >> shift) - (bias >> shift));
}

int shift_down(int v, unsigned shift)
{
    return static_cast<int>(shift_down(int64_t{v}, shift));
}

// The low bits of v's two's complement, for hashing.
uint64_t low_bits(int v, uint32_t mask)
{
    return uint64_t{static_cast<uint32_t>(v) & mask};
}

struct Logistic
{
    std::array<int16_t, 4096> stretch{};
    std::array<int16_t, 4096> squash{}; // by x + 2048

    Logistic()
    {
        const auto exact = [](int x) {
            return std::clamp(static_cast<int>(4096.0 / (1.0 + std::exp(-x / 256.0))), 1, 4095);
        };
        for (std::size_t i = 0; i < squash.size(); ++i)
        {
            squash[i] = static_cast<int16_t>(exact(static_cast<int>(i) - 2048));
        }
        int previous = 0;
        for (int x = -2047; x <= 2047; ++x)
        {
            const int v = exact(x);
            for (int p = previous; p <= v; ++p)
            {
                stretch[static_cast<std::size_t>(p)] = static_cast<int16_t>(x);
            }
            previous = v + 1;
        }
        for (int p = previous; p < 4096; ++p)
        {
            stretch[static_cast<std::size_t>(p)] = 2047;
        }
    }
};

const Logistic& logistic()
{
    static const Logistic tables;
    return tables;
}

int squash(int x)
{
    const int index = std::clamp(x, -2047, 2047) + 2048;
    return logistic().squash[static_cast<std::size_t>(index)];
}

int stretch(int p)
{
    return logistic().stretch[static_cast<std::size_t>(p)];
}

// A 16-bit probability whose rate falls from 1/2 to 1/(limit + 2) as it sees bits.
struct Counter
{
    uint16_t p = 32768;
    uint8_t n = 0;

    int p12() const
    {
        return std::clamp(p >> 4U, 1, 4095);
    }

    void update(unsigned bit, unsigned limit)
    {
        const int target = bit != 0 ? 65535 : 0;
        p = static_cast<uint16_t>(p + ((target - p) / static_cast<int>(n + 2)));
        if (n < limit)
        {
            ++n;
        }
    }
};

class Mixer
{
public:
    static constexpr std::size_t inputs = 9;

    explicit Mixer(std::size_t sets)
        : weights_(sets * inputs, 16384)
    {
    }

    int mix(const std::array<int, inputs>& in, std::size_t set)
    {
        set_ = set * inputs;
        int64_t dot = 0;
        for (std::size_t i = 0; i < inputs; ++i)
        {
            dot += int64_t{in[i]} * weights_[set_ + i];
        }
        p_ = squash(static_cast<int>(std::clamp<int64_t>(shift_down(dot, 16), -2047, 2047)));
        return p_;
    }

    void update(const std::array<int, inputs>& in, unsigned bit)
    {
        const int error = static_cast<int>(bit << 12U) - p_;
        for (std::size_t i = 0; i < inputs; ++i)
        {
            weights_[set_ + i] += shift_down(in[i] * error, 12);
        }
    }

private:
    std::vector<int> weights_;
    std::size_t set_ = 0;
    int p_ = 2048;
};

// Refines a probability in a small context: 33 interpolated buckets over the stretched input.
class Apm
{
public:
    explicit Apm(std::size_t contexts)
        : table_(contexts * 33)
    {
        for (std::size_t c = 0; c < contexts; ++c)
        {
            for (std::size_t j = 0; j < 33; ++j)
            {
                table_[(c * 33) + j] =
                    static_cast<uint16_t>(squash((static_cast<int>(j) - 16) * 128) * 16);
            }
        }
    }

    int p(int probability, std::size_t context)
    {
        const auto s = static_cast<unsigned>(stretch(probability) + 2048);
        weight_ = static_cast<int>(s & 127U);
        index_ = (context * 33) + (s >> 7U);
        return static_cast<int>(((unsigned{table_[index_]} * static_cast<unsigned>(128 - weight_)) +
                                 (unsigned{table_[index_ + 1]} * static_cast<unsigned>(weight_))) >>
                                11U);
    }

    void update(unsigned bit)
    {
        const int target = bit != 0 ? 65535 : 0;
        const auto move = [&](uint16_t& entry, int share) {
            entry = static_cast<uint16_t>(entry + ((target - entry) * share / (128 * 64)));
        };
        move(table_[index_], 128 - weight_);
        move(table_[index_ + 1], weight_);
    }

private:
    std::vector<uint16_t> table_;
    std::size_t index_ = 0;
    int weight_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Binary arithmetic coder, 32 bits, carry-less, as in the CM coder.

class BitEncoder
{
public:
    explicit BitEncoder(std::vector<uint8_t>& out)
        : out_(out)
    {
    }

    void encode(unsigned bit, int p)
    {
        const uint32_t mid = low_ + static_cast<uint32_t>(
                                        (uint64_t{high_ - low_} * static_cast<uint32_t>(p)) >> 12U);
        if (bit != 0)
        {
            high_ = mid;
        }
        else
        {
            low_ = mid + 1;
        }
        while (((low_ ^ high_) & 0xFF000000U) == 0)
        {
            out_.push_back(static_cast<uint8_t>(high_ >> 24U));
            low_ <<= 8U;
            high_ = (high_ << 8U) | 0xFFU;
        }
    }

    void finish()
    {
        for (int i = 0; i < 4; ++i)
        {
            out_.push_back(static_cast<uint8_t>(low_ >> 24U));
            low_ <<= 8U;
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint32_t low_ = 0;
    uint32_t high_ = 0xFFFFFFFFU;
};

class BitDecoder
{
public:
    explicit BitDecoder(std::span<const uint8_t> in)
        : in_(in)
    {
        for (int i = 0; i < 4; ++i)
        {
            x_ = (x_ << 8U) | next();
        }
    }

    unsigned decode(int p)
    {
        const uint32_t mid = low_ + static_cast<uint32_t>(
                                        (uint64_t{high_ - low_} * static_cast<uint32_t>(p)) >> 12U);
        const unsigned bit = x_ <= mid ? 1U : 0U;
        if (bit != 0)
        {
            high_ = mid;
        }
        else
        {
            low_ = mid + 1;
        }
        while (((low_ ^ high_) & 0xFF000000U) == 0)
        {
            low_ <<= 8U;
            high_ = (high_ << 8U) | 0xFFU;
            x_ = (x_ << 8U) | next();
        }
        return bit;
    }

    // Whether every byte was used and no more.
    bool consumed_exactly() const
    {
        return at_ == in_.size();
    }

    bool overran() const
    {
        return at_ > in_.size();
    }

private:
    uint8_t next()
    {
        const uint8_t byte = at_ < in_.size() ? in_[at_] : uint8_t{0};
        ++at_;
        return byte;
    }

    std::span<const uint8_t> in_;
    std::size_t at_ = 0;
    uint32_t low_ = 0;
    uint32_t high_ = 0xFFFFFFFFU;
    uint32_t x_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Colour transforms and predictors

enum class ColourTransform : uint8_t
{
    none = 0,
    subtract_green = 1, // G, R - G, B - G
    ycocg = 2,          // YCoCg-R
};
constexpr std::size_t colour_transforms = 3;

enum class Predictor : uint8_t
{
    left = 0,
    up = 1,
    average = 2,
    paeth = 3,
    med = 4,
    gap = 5,
};
constexpr std::size_t predictors = 6;

// A channel's values after the transform, and their range.
struct Plane
{
    std::vector<int> values;
    int low = 0;
    int high = 255;
};

std::vector<Plane> to_planes(std::span<const uint8_t> pixels, std::size_t count, unsigned channels,
                             ColourTransform transform)
{
    std::vector<Plane> planes(channels);
    for (Plane& plane : planes)
    {
        plane.values.resize(count);
    }
    const bool colour = channels >= 3;
    if (colour && transform != ColourTransform::none)
    {
        planes[1].low = planes[2].low = -255;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const uint8_t* pixel = pixels.data() + (i * channels);
        if (colour)
        {
            const int r = pixel[0];
            const int g = pixel[1];
            const int b = pixel[2];
            if (transform == ColourTransform::subtract_green)
            {
                planes[0].values[i] = g;
                planes[1].values[i] = r - g;
                planes[2].values[i] = b - g;
            }
            else if (transform == ColourTransform::ycocg)
            {
                const int co = r - b;
                const int t = b + shift_down(co, 1);
                const int cg = g - t;
                planes[0].values[i] = t + shift_down(cg, 1);
                planes[1].values[i] = co;
                planes[2].values[i] = cg;
            }
            else
            {
                planes[0].values[i] = r;
                planes[1].values[i] = g;
                planes[2].values[i] = b;
            }
            for (unsigned c = 3; c < channels; ++c)
            {
                planes[c].values[i] = pixel[c];
            }
        }
        else
        {
            for (unsigned c = 0; c < channels; ++c)
            {
                planes[c].values[i] = pixel[c];
            }
        }
    }
    return planes;
}

void from_planes(const std::vector<Plane>& planes, std::size_t count, unsigned channels,
                 ColourTransform transform, std::span<uint8_t> pixels)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        uint8_t* pixel = pixels.data() + (i * channels);
        if (channels >= 3)
        {
            const int a = planes[0].values[i];
            const int b = planes[1].values[i];
            const int c = planes[2].values[i];
            int r = a;
            int g = b;
            int bl = c;
            if (transform == ColourTransform::subtract_green)
            {
                g = a;
                r = b + a;
                bl = c + a;
            }
            else if (transform == ColourTransform::ycocg)
            {
                const int t = a - shift_down(c, 1);
                g = c + t;
                bl = t - shift_down(b, 1);
                r = bl + b;
            }
            pixel[0] = static_cast<uint8_t>(r);
            pixel[1] = static_cast<uint8_t>(g);
            pixel[2] = static_cast<uint8_t>(bl);
            for (unsigned ch = 3; ch < channels; ++ch)
            {
                pixel[ch] = static_cast<uint8_t>(planes[ch].values[i]);
            }
        }
        else
        {
            for (unsigned ch = 0; ch < channels; ++ch)
            {
                pixel[ch] = static_cast<uint8_t>(planes[ch].values[i]);
            }
        }
    }
}

struct Neighbours
{
    int w;
    int n;
    int nw;
    int ne;
    int ww;
    int nn;
};

Neighbours neighbours(const std::vector<int>& v, std::size_t width, std::size_t x, std::size_t y,
                      int middle)
{
    const auto at = [&](std::size_t px, std::size_t py) {
        return v[(py * width) + px];
    };
    Neighbours n{};
    n.w = x > 0 ? at(x - 1, y) : (y > 0 ? at(x, y - 1) : middle);
    n.n = y > 0 ? at(x, y - 1) : n.w;
    n.nw = x > 0 && y > 0 ? at(x - 1, y - 1) : n.n;
    n.ne = y > 0 && x + 1 < width ? at(x + 1, y - 1) : n.n;
    n.ww = x > 1 ? at(x - 2, y) : n.w;
    n.nn = y > 1 ? at(x, y - 2) : n.n;
    return n;
}

int predict(Predictor predictor, const Neighbours& n, int low, int high)
{
    switch (predictor)
    {
    case Predictor::left:
        return n.w;
    case Predictor::up:
        return n.n;
    case Predictor::average:
        return shift_down(n.w + n.n, 1);
    case Predictor::paeth:
    {
        const int p = n.w + n.n - n.nw;
        const int pa = std::abs(p - n.w);
        const int pb = std::abs(p - n.n);
        const int pc = std::abs(p - n.nw);
        return pa <= pb && pa <= pc ? n.w : (pb <= pc ? n.n : n.nw);
    }
    case Predictor::med:
    {
        const int most = std::max(n.w, n.n);
        const int least = std::min(n.w, n.n);
        return n.nw >= most ? least : (n.nw <= least ? most : n.w + n.n - n.nw);
    }
    case Predictor::gap:
        break;
    }
    // CALIC's gradient-adjusted prediction, in quarters.
    const int horizontal = std::abs(n.w - n.ww) + std::abs(n.n - n.nw) + std::abs(n.n - n.ne);
    const int vertical = std::abs(n.w - n.nw) + std::abs(n.n - n.nn) + std::abs(n.ne - n.nn);
    const int difference = vertical - horizontal;
    int p4 = 0;
    if (difference > 80)
    {
        p4 = 4 * n.w;
    }
    else if (difference < -80)
    {
        p4 = 4 * n.n;
    }
    else
    {
        p4 = (2 * (n.w + n.n)) + (n.ne - n.nw);
        if (difference > 32)
        {
            p4 = (p4 + (4 * n.w)) / 2;
        }
        else if (difference > 8)
        {
            p4 = ((3 * p4) + (4 * n.w)) / 4;
        }
        else if (difference < -32)
        {
            p4 = (p4 + (4 * n.n)) / 2;
        }
        else if (difference < -8)
        {
            p4 = ((3 * p4) + (4 * n.n)) / 4;
        }
    }
    return std::clamp(shift_down(p4 + 2, 2), low, high);
}

unsigned bit_length(unsigned v)
{
    return static_cast<unsigned>(std::bit_width(v));
}

unsigned activity_bucket(const Neighbours& n)
{
    const auto activity =
        static_cast<unsigned>(std::abs(n.w - n.nw) + std::abs(n.n - n.nw) + std::abs(n.n - n.ne) +
                              std::abs(n.w - n.ww) + std::abs(n.n - n.nn));
    return std::min(15U, bit_length(activity));
}

// The residual's range: 8 bits for a plain channel, 9 for a difference.
unsigned residual_bits(const Plane& plane)
{
    return plane.low < 0 ? 9U : 8U;
}

int wrap(int v, unsigned bits)
{
    const auto range = static_cast<int>(1U << bits);
    v = static_cast<int>(static_cast<unsigned>(v) & ((1U << bits) - 1U));
    return v >= range / 2 ? v - range : v;
}

// The plane value of prediction + residual, wrapping as the encoder did.
int unwrap(int prediction, int residual, const Plane& plane, unsigned bits)
{
    const auto range = static_cast<int>(1U << bits);
    int v = prediction + residual;
    while (v > plane.high)
    {
        v -= range;
    }
    while (v < plane.low)
    {
        v += range;
    }
    return v;
}

// ---------------------------------------------------------------------------------------------
// Choosing the transform and predictors: residual entropy given the activity, on every other row.

double estimate_bits(const Plane& plane, std::size_t width, std::size_t rows, Predictor predictor)
{
    const unsigned bits = residual_bits(plane);
    const int middle = (plane.low + plane.high + 1) / 2;
    std::vector<uint32_t> counts(std::size_t{16} << bits, 0);
    for (std::size_t y = 0; y < rows; y += 2)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            const Neighbours n = neighbours(plane.values, width, x, y, middle);
            const int r = wrap(
                plane.values[(y * width) + x] - predict(predictor, n, plane.low, plane.high), bits);
            ++counts[(std::size_t{activity_bucket(n)} << bits) +
                     static_cast<std::size_t>(r + static_cast<int>(1U << (bits - 1)))];
        }
    }
    double total = 0;
    for (std::size_t bucket = 0; bucket < 16; ++bucket)
    {
        const auto first = counts.begin() + static_cast<std::ptrdiff_t>(bucket << bits);
        const auto last = first + static_cast<std::ptrdiff_t>(std::size_t{1} << bits);
        double n = 0;
        for (auto it = first; it != last; ++it)
        {
            n += *it;
        }
        for (auto it = first; it != last; ++it)
        {
            if (*it != 0)
            {
                total -= *it * std::log2(*it / n);
            }
        }
    }
    return total;
}

struct Choice
{
    ColourTransform transform = ColourTransform::none;
    std::array<Predictor, 4> predictors{};
};

Choice choose(std::span<const uint8_t> pixels, std::size_t width, std::size_t rows,
              unsigned channels)
{
    Choice best;
    double best_bits = std::numeric_limits<double>::max();
    const std::size_t transforms = channels >= 3 ? colour_transforms : 1;
    for (std::size_t t = 0; t < transforms; ++t)
    {
        const auto transform = static_cast<ColourTransform>(t);
        const std::vector<Plane> planes = to_planes(pixels, width * rows, channels, transform);
        Choice choice{.transform = transform, .predictors = {}};
        double bits = 0;
        for (unsigned c = 0; c < channels; ++c)
        {
            double channel_best = std::numeric_limits<double>::max();
            for (std::size_t p = 0; p < predictors; ++p)
            {
                const double estimate =
                    estimate_bits(planes[c], width, rows, static_cast<Predictor>(p));
                if (estimate < channel_best)
                {
                    channel_best = estimate;
                    choice.predictors[c] = static_cast<Predictor>(p);
                }
            }
            bits += channel_best;
        }
        if (bits < best_bits)
        {
            best_bits = bits;
            best = choice;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------
// The residual model

// Decision ids: 0 zero, 1 sign, 2-10 the magnitude's bit length (unary), 16 + 9k + i mantissa
// bit i of a magnitude with bit length k + 1.
constexpr std::size_t decisions = 128;
// The hashed tables' sizes follow the strip's sample count, so small images don't pay for large
// tables: at most these many bits of index.
constexpr unsigned max_hash_bits = 22;
constexpr unsigned direct_limit = 255;
constexpr unsigned hashed_limit = 60;
constexpr unsigned max_match_bits = 18;

unsigned table_bits(std::size_t samples, unsigned low, unsigned high)
{
    return std::clamp(static_cast<unsigned>(std::bit_width(samples)) + 1, low, high);
}

unsigned magnitude_class(int e)
{
    return std::min(7U, bit_length(static_cast<unsigned>(std::abs(e))));
}

unsigned signed_class(int v)
{
    const auto m = std::min(7U, bit_length(static_cast<unsigned>(std::abs(v))));
    return v < 0 ? 7 - m : 7 + m; // 0-14
}

class ResidualModel
{
public:
    explicit ResidualModel(unsigned hash_bits)
        : hash_bits_(hash_bits)
        , activity_(std::size_t{4} * 16 * decisions)
        , errors_(std::size_t{4} * 64 * decisions)
        , texture_(std::size_t{4} * 256 * decisions)
        , cross_(std::size_t{4} * 256 * decisions)
        , shape_(std::size_t{4} * 256 * decisions)
        , hashed_(std::size_t{1} << hash_bits)
        , matched_(std::size_t{1} << hash_bits)
        , mixer_(std::size_t{4} * 12 * 4 * 3)
        , apm_(std::size_t{4} * decisions * 4)
    {
    }

    // Sets the contexts for one sample.
    // `match_state`: 0 without a match candidate, else 1 + its run (0-3) + 4 when this pixel's
    // earlier channels equalled the candidate's; `match_delta` the candidate minus the prediction.
    void start(unsigned channel, const Neighbours& n, int prediction, int error_w, int error_n,
               int error_nw, int previous_channel_error, int first_channel_error,
               unsigned match_state, int match_delta)
    {
        channel_ = channel;
        const unsigned activity = activity_bucket(n);
        activity_index_ = ((channel * 16) + activity) * decisions;
        errors_index_ =
            ((channel * 64) + (magnitude_class(error_w) * 8) + magnitude_class(error_n)) *
            decisions;
        const unsigned pattern = (n.n > prediction ? 1U : 0U) | (n.w > prediction ? 2U : 0U) |
                                 (n.nw > prediction ? 4U : 0U) | (n.ne > prediction ? 8U : 0U) |
                                 (n.nn > prediction ? 16U : 0U) | (n.ww > prediction ? 32U : 0U);
        texture_index_ = ((channel * 256) + (pattern * 4) + std::min(3U, activity / 3)) * decisions;
        const unsigned cross =
            channel == 0
                ? (magnitude_class(error_nw) * 16) + signed_class(error_w + error_n)
                : (signed_class(previous_channel_error) * 16) + signed_class(first_channel_error);
        cross_index_ = ((channel * 256) + std::min(255U, cross)) * decisions;
        shape_index_ =
            ((channel * 256) + (signed_class(n.n - n.w) * 16) + signed_class(n.ne - n.n)) *
            decisions;
        uint64_t h = (uint64_t{channel} << 56U) ^ (low_bits(n.w, 0x3FF) << 40U) ^
                     (low_bits(n.n, 0x3FF) << 30U) ^ (low_bits(n.nw, 0x3FF) << 20U) ^
                     (low_bits(n.ne, 0x3FF) << 10U) ^ low_bits(prediction, 0x3FF);
        h *= 0x9E3779B97F4A7C15ULL;
        hash_ = h >> (64U - hash_bits_);
        const uint64_t m = ((uint64_t{channel} << 20U) | (uint64_t{match_state} << 12U) |
                            low_bits(match_delta, 0xFFF)) *
                           0xC2B2AE3D27D4EB4FULL;
        match_hash_ = m >> (64U - hash_bits_);
        const unsigned strength = match_state == 0 ? 0U : (match_state >= 4 ? 2U : 1U);
        mixer_base_ = ((((channel * 12) * 4) + std::min(3U, activity / 4)) * 3) + strength;
        apm_base_ = (channel * decisions * 4) + std::min(3U, activity / 4);
    }

    int p(std::size_t decision)
    {
        decision_ = decision;
        counters_ = {
            &activity_[activity_index_ + decision],
            &errors_[errors_index_ + decision],
            &texture_[texture_index_ + decision],
            &cross_[cross_index_ + decision],
            &shape_[shape_index_ + decision],
            &hashed_[(hash_ + (decision * 0x9E3779B1U)) & ((std::size_t{1} << hash_bits_) - 1)],
            &matched_[(match_hash_ + (decision * 0x85EBCA6BU)) &
                      ((std::size_t{1} << hash_bits_) - 1)]};
        for (std::size_t i = 0; i < counters_.size(); ++i)
        {
            inputs_[i] = stretch(counters_[i]->p12());
        }
        inputs_[7] = (inputs_[0] + inputs_[5]) / 2;
        inputs_[8] = 256;
        const std::size_t group =
            decision < 2 ? decision
                         : (decision < 16 ? 2 + std::min<std::size_t>(decision - 2, 5)
                                          : 8 + std::min<std::size_t>((decision - 16) / 9, 3));
        const int mixed = mixer_.mix(inputs_, mixer_base_ + (group * 4 * 3));
        const int refined = apm_.p(mixed, apm_base_ + (decision * 4));
        p_ = std::clamp((mixed + (3 * refined)) / 4, 1, 4095);
        return p_;
    }

    void update(unsigned bit)
    {
        for (std::size_t i = 0; i < 5; ++i)
        {
            counters_[i]->update(bit, direct_limit);
        }
        counters_[5]->update(bit, hashed_limit);
        counters_[6]->update(bit, hashed_limit);
        mixer_.update(inputs_, bit);
        apm_.update(bit);
    }

private:
    unsigned hash_bits_;
    unsigned channel_ = 0;
    std::vector<Counter> activity_;
    std::vector<Counter> errors_;
    std::vector<Counter> texture_;
    std::vector<Counter> cross_;
    std::vector<Counter> shape_;
    std::vector<Counter> hashed_;
    std::vector<Counter> matched_;
    Mixer mixer_;
    Apm apm_;
    std::size_t activity_index_ = 0;
    std::size_t errors_index_ = 0;
    std::size_t texture_index_ = 0;
    std::size_t cross_index_ = 0;
    std::size_t shape_index_ = 0;
    std::size_t hash_ = 0;
    std::size_t match_hash_ = 0;
    std::size_t mixer_base_ = 0;
    std::size_t apm_base_ = 0;
    std::size_t decision_ = 0;
    std::array<Counter*, 7> counters_{};
    std::array<int, Mixer::inputs> inputs_{};
    int p_ = 2048;
};

// Whether a whole pixel equals one of a few earlier ones (W, N, NE, NW, the match candidate): flat
// graphics and screens are mostly such copies.
constexpr std::size_t copy_candidates = 5;

class CopyModel
{
public:
    explicit CopyModel(unsigned colour_bits)
        : colour_bits_(colour_bits)
        , smooth_(copy_candidates * 16 * 2 * 8)
        , around_(copy_candidates * 7 * 7 * 2)
        , equalities_(copy_candidates * 64 * 8)
        , runs_(copy_candidates * 8 * 16)
        , colours_(std::size_t{1} << colour_bits)
        , mixer_(copy_candidates * 8 * 4)
        , apm_(copy_candidates * 8)
    {
    }

    // `equalities`: which of W, N, NW, NE and the candidate equal each other (6 bits);
    // `previous`: what the pixel before did (0-4 a copy, 5 none, 6 a row's start); `run`: the
    // match candidate's run; `colours`: a hash of W's and N's values.
    // `activity`: the first channel's activity bucket; `predicted`: the candidate equals the
    // predictions; `left`, `up`: what W and N did (as `previous`).
    int p(std::size_t candidate, unsigned equalities, unsigned previous, unsigned run,
          uint64_t colours, unsigned activity, bool predicted, unsigned left, unsigned up)
    {
        counters_ = {
            &equalities_[(((candidate * 64) + equalities) * 8) + previous],
            &runs_[(((candidate * 8) + previous) * 16) + std::min(run, 15U)],
            &colours_[((colours + (candidate * 0x9E3779B97F4A7C15ULL)) * 0xC2B2AE3D27D4EB4FULL) >>
                      (64U - colour_bits_)],
            &smooth_[(((((candidate * 16) + activity) * 2) + (predicted ? 1 : 0)) * 8) + previous],
            &around_[(((((candidate * 7) + left) * 7) + up) * 2) + (predicted ? 1 : 0)]};
        for (std::size_t i = 0; i < counters_.size(); ++i)
        {
            inputs_[i] = stretch(counters_[i]->p12());
        }
        inputs_[5] = 256;
        for (std::size_t i = 6; i < Mixer::inputs; ++i)
        {
            inputs_[i] = 0;
        }
        const std::size_t set = (candidate * 8) + previous;
        const int mixed = mixer_.mix(inputs_, (set * 4) + std::min(3U, activity / 3));
        const int refined = apm_.p(mixed, set);
        return std::clamp((mixed + (3 * refined)) / 4, 1, 4095);
    }

    void update(unsigned bit)
    {
        counters_[0]->update(bit, direct_limit);
        counters_[1]->update(bit, direct_limit);
        counters_[2]->update(bit, hashed_limit);
        counters_[3]->update(bit, direct_limit);
        counters_[4]->update(bit, direct_limit);
        mixer_.update(inputs_, bit);
        apm_.update(bit);
    }

private:
    unsigned colour_bits_;
    std::vector<Counter> smooth_;
    std::vector<Counter> around_;
    std::vector<Counter> equalities_;
    std::vector<Counter> runs_;
    std::vector<Counter> colours_;
    Mixer mixer_;
    Apm apm_;
    std::array<Counter*, 5> counters_{};
    std::array<int, Mixer::inputs> inputs_{};
};

// CALIC's bias cancellation: the mean error in a context of activity and texture, added to the
// prediction.
class BiasCorrection
{
public:
    explicit BiasCorrection(unsigned channels)
        : sums_(std::size_t{channels} * 1024, 0)
        , counts_(std::size_t{channels} * 1024, 0)
    {
    }

    int correction(unsigned channel, const Neighbours& n, int prediction)
    {
        const unsigned pattern = (n.n > prediction ? 1U : 0U) | (n.w > prediction ? 2U : 0U) |
                                 (n.nw > prediction ? 4U : 0U) | (n.ne > prediction ? 8U : 0U) |
                                 (n.nn > prediction ? 16U : 0U) | (n.ww > prediction ? 32U : 0U);
        index_ = (std::size_t{channel} * 1024) + (std::size_t{pattern} * 16) + activity_bucket(n);
        const int count = counts_[index_];
        if (count == 0)
        {
            return 0;
        }
        const int sum = sums_[index_];
        return sum >= 0 ? (sum + (count / 2)) / count : -((-sum + (count / 2)) / count);
    }

    void update(int error)
    {
        sums_[index_] += error;
        if (++counts_[index_] == 64)
        {
            sums_[index_] /= 2;
            counts_[index_] = 32;
        }
    }

private:
    std::vector<int> sums_;
    std::vector<int> counts_;
    std::size_t index_ = 0;
};

// Codes one strip's planes (`encode`) or fills them (`!encode`) sample by sample.
template <class Coder>
bool code_strip(std::vector<Plane>& planes, std::size_t width, std::size_t rows,
                const Choice& choice, Coder& coder)
{
    constexpr bool encoding = requires(Coder& c) { c.encode(0U, 0); };
    const auto channels = static_cast<unsigned>(planes.size());
    const std::size_t samples = width * rows * channels;
    const unsigned match_bits = table_bits(width * rows, 10, max_match_bits);
    ResidualModel model(table_bits(samples, 12, max_hash_bits));
    CopyModel copies(table_bits(width * rows, 10, 18));
    unsigned previous_copy = 6;
    std::vector<uint8_t> outcomes(width * rows, 6); // what each pixel did, as previous_copy
    BiasCorrection bias(channels);
    std::vector<std::vector<int>> errors(channels, std::vector<int>(width * rows, 0));
    // The last pixel after each neighbourhood (W, N, NW, NE, WW of every channel), plus 1.
    std::vector<uint32_t> last_seen(std::size_t{1} << match_bits, 0);
    // The same for a wider neighbourhood, which tells repeated glyphs apart better.
    std::vector<uint32_t> last_seen_wide(std::size_t{1} << match_bits, 0);
    unsigned run = 0;
    const auto code = [&](std::size_t decision, unsigned bit) -> unsigned {
        const int p = model.p(decision);
        if constexpr (encoding)
        {
            coder.encode(bit, p);
        }
        else
        {
            bit = coder.decode(p);
        }
        model.update(bit);
        return bit;
    };
    for (std::size_t y = 0; y < rows; ++y)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            const std::size_t i = (y * width) + x;
            int first_error = 0;
            int previous_error = 0;
            uint64_t key = 0x9E3779B97F4A7C15ULL;
            for (const Plane& plane : planes)
            {
                const Neighbours n = neighbours(plane.values, width, x, y, 0);
                for (const int v : {n.w, n.n, n.nw, n.ne, n.ww})
                {
                    key = (key ^ low_bits(v, 0x3FF)) * 0x100000001B3ULL;
                }
            }
            const auto slot =
                static_cast<std::size_t>((key * 0xC2B2AE3D27D4EB4FULL) >> (64U - match_bits));
            uint64_t wide = key;
            const bool has_wide = x > 2 && y > 1 && x + 2 < width;
            if (has_wide)
            {
                for (const Plane& plane : planes)
                {
                    const auto at = [&](std::size_t px, std::size_t py) {
                        return plane.values[(py * width) + px];
                    };
                    for (const int v :
                         {at(x - 3, y), at(x - 2, y - 1), at(x + 1, y - 2), at(x, y - 2),
                          at(x - 1, y - 2), at(x + 2, y - 1), at(x + 2, y - 2)})
                    {
                        wide = (wide ^ low_bits(v, 0x3FF)) * 0x100000001B3ULL;
                    }
                }
            }
            const auto wide_slot =
                static_cast<std::size_t>((wide * 0x9E3779B97F4A7C15ULL) >> (64U - match_bits));
            uint32_t candidate = has_wide ? last_seen_wide[wide_slot] : 0;
            if (candidate == 0 && x > 1 && y > 0)
            {
                candidate = last_seen[slot];
            }

            // A copy of a whole earlier pixel, tried in order, each colour once.
            const auto same = [&](std::size_t a, std::size_t b) {
                return std::ranges::all_of(planes, [&](const Plane& plane) {
                    return plane.values[a] == plane.values[b];
                });
            };
            constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
            const std::array<std::size_t, copy_candidates> sources{
                x > 0 ? i - 1 : none, y > 0 ? i - width : none,
                y > 0 && x + 1 < width ? i - width + 1 : none,
                x > 0 && y > 0 ? i - width - 1 : none, candidate != 0 ? candidate - 1 : none};
            unsigned equalities = 0;
            {
                unsigned bit = 0;
                for (std::size_t a = 0; a < 4; ++a)
                {
                    for (std::size_t b = a + 1; b < copy_candidates && bit < 6; ++b, ++bit)
                    {
                        if (sources[a] != none && sources[b] != none &&
                            same(sources[a], sources[b]))
                        {
                            equalities |= 1U << bit;
                        }
                    }
                }
            }
            uint64_t colours = 0;
            for (const Plane& plane : planes)
            {
                colours = (colours << 10U) ^
                          low_bits(sources[0] != none ? plane.values[sources[0]] : 0, 0x3FF);
                colours = (colours << 10U) ^
                          low_bits(sources[1] != none ? plane.values[sources[1]] : 0, 0x3FF);
            }
            std::size_t copied = none;
            for (std::size_t j = 0; j < copy_candidates && copied == none; ++j)
            {
                if (sources[j] == none ||
                    std::ranges::any_of(std::span(sources).first(j), [&](std::size_t earlier) {
                    return earlier != none && same(earlier, sources[j]);
                }))
                {
                    continue;
                }
                bool predicted = true;
                for (unsigned c = 0; c < channels && predicted; ++c)
                {
                    const Plane& plane = planes[c];
                    const Neighbours n =
                        neighbours(plane.values, width, x, y, (plane.low + plane.high + 1) / 2);
                    predicted = predict(choice.predictors[c], n, plane.low, plane.high) ==
                                plane.values[sources[j]];
                }
                const Neighbours first_neighbours = neighbours(
                    planes[0].values, width, x, y, (planes[0].low + planes[0].high + 1) / 2);
                const unsigned left = x > 0 ? outcomes[i - 1] : 6U;
                const unsigned up = y > 0 ? outcomes[i - width] : 6U;
                const int p = copies.p(j, equalities, previous_copy, run, colours,
                                       activity_bucket(first_neighbours), predicted, left, up);
                unsigned bit = 0;
                if constexpr (encoding)
                {
                    bit = same(i, sources[j]) ? 1U : 0U;
                    coder.encode(bit, p);
                }
                else
                {
                    bit = coder.decode(p);
                }
                copies.update(bit);
                if (bit != 0)
                {
                    copied = j;
                }
            }
            outcomes[i] = static_cast<uint8_t>(copied == none ? 5U : copied);
            previous_copy = x + 1 == width ? 6U : outcomes[i];
            if (copied != none)
            {
                for (unsigned c = 0; c < channels; ++c)
                {
                    Plane& plane = planes[c];
                    if constexpr (!encoding)
                    {
                        plane.values[i] = plane.values[sources[copied]];
                    }
                    const Neighbours n =
                        neighbours(plane.values, width, x, y, (plane.low + plane.high + 1) / 2);
                    errors[c][i] =
                        plane.values[i] - predict(choice.predictors[c], n, plane.low, plane.high);
                }
                const bool matched = candidate != 0 && same(i, candidate - 1);
                run = matched ? run + 1 : 0;
                if (x > 1 && y > 0)
                {
                    last_seen[slot] = static_cast<uint32_t>(i + 1);
                }
                if (has_wide)
                {
                    last_seen_wide[wide_slot] = static_cast<uint32_t>(i + 1);
                }
                continue;
            }
            bool agree = true;
            for (unsigned c = 0; c < channels; ++c)
            {
                Plane& plane = planes[c];
                const unsigned bits = residual_bits(plane);
                const int middle = (plane.low + plane.high + 1) / 2;
                const Neighbours n = neighbours(plane.values, width, x, y, middle);
                const int base = predict(choice.predictors[c], n, plane.low, plane.high);
                const int prediction =
                    std::clamp(base + bias.correction(c, n, base), plane.low, plane.high);
                const std::vector<int>& e = errors[c];
                const int error_w = x > 0 ? e[i - 1] : 0;
                const int error_n = y > 0 ? e[i - width] : 0;
                const int error_nw = x > 0 && y > 0 ? e[i - width - 1] : 0;
                unsigned match_state = 0;
                int match_delta = 0;
                if (candidate != 0)
                {
                    match_state = 1 + std::min(run, 3U) + (agree && c > 0 ? 4U : 0U);
                    match_delta = plane.values[candidate - 1] - prediction;
                }
                model.start(c, n, prediction, error_w, error_n, error_nw, previous_error,
                            first_error, match_state, match_delta);

                int residual = 0;
                if constexpr (encoding)
                {
                    residual = wrap(plane.values[i] - prediction, bits);
                }
                const unsigned zero = code(0, residual == 0 ? 1U : 0U);
                if (zero == 0)
                {
                    const unsigned negative = code(1, residual < 0 ? 1U : 0U);
                    const auto magnitude = static_cast<unsigned>(std::abs(residual));
                    const unsigned length = encoding ? bit_length(magnitude) : 0; // 1..bits
                    unsigned k = 0;
                    while (k + 1 < bits && code(2 + k, length > k + 1 ? 1U : 0U) != 0)
                    {
                        ++k;
                    }
                    unsigned value = 1;
                    for (unsigned b = k; b-- > 0;)
                    {
                        value = (value << 1U) | code(16 + (k * 9) + b, (magnitude >> b) & 1U);
                    }
                    residual = negative != 0 ? -static_cast<int>(value) : static_cast<int>(value);
                }
                if constexpr (!encoding)
                {
                    plane.values[i] = unwrap(prediction, residual, plane, bits);
                }
                const int error = plane.values[i] - prediction;
                bias.update(plane.values[i] - base);
                errors[c][i] = error;
                if (c == 0)
                {
                    first_error = error;
                }
                previous_error = error;
                agree = agree && candidate != 0 && plane.values[candidate - 1] == plane.values[i];
            }
            run = agree ? run + 1 : 0;
            if (x > 1 && y > 0)
            {
                last_seen[slot] = static_cast<uint32_t>(i + 1);
            }
            if (has_wide)
            {
                last_seen_wide[wide_slot] = static_cast<uint32_t>(i + 1);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// File layout (docs/file_format.md, "Image file")

constexpr std::array<uint8_t, 4> image_magic{0x89, 'I', 'N', 'I'};
constexpr uint8_t image_version = 1;
constexpr std::size_t header_bytes = 4 + 1 + 1 + 1 + 4 + 4 + 4 + 4;
// Strips of about this many pixels, so a large image codes on several threads.
constexpr std::size_t strip_pixels = std::size_t{1} << 19U;
constexpr std::size_t max_pixels_per_byte = 32768;

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
}

uint32_t get32(std::span<const uint8_t> bytes, std::size_t at)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        v |= uint32_t{bytes[at + i]} << (8 * i);
    }
    return v;
}

std::vector<uint8_t> encode_strip(std::span<const uint8_t> pixels, std::size_t width,
                                  std::size_t rows, unsigned channels)
{
    const Choice choice = choose(pixels, width, rows, channels);
    std::vector<Plane> planes = to_planes(pixels, width * rows, channels, choice.transform);
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(choice.transform));
    for (unsigned c = 0; c < channels; ++c)
    {
        out.push_back(static_cast<uint8_t>(choice.predictors[c]));
    }
    BitEncoder encoder(out);
    code_strip(planes, width, rows, choice, encoder);
    encoder.finish();
    return out;
}

std::expected<void, Error> decode_strip(std::span<const uint8_t> payload, std::size_t width,
                                        std::size_t rows, unsigned channels,
                                        std::span<uint8_t> pixels)
{
    if (payload.size() < 1 + channels || payload[0] >= colour_transforms ||
        (channels < 3 && payload[0] != 0))
    {
        return std::unexpected(Error::corrupt_payload);
    }
    Choice choice{.transform = static_cast<ColourTransform>(payload[0]), .predictors = {}};
    for (unsigned c = 0; c < channels; ++c)
    {
        if (payload[1 + c] >= predictors)
        {
            return std::unexpected(Error::corrupt_payload);
        }
        choice.predictors[c] = static_cast<Predictor>(payload[1 + c]);
    }
    std::vector<Plane> planes = to_planes({}, 0, channels, choice.transform);
    for (Plane& plane : planes)
    {
        plane.values.assign(width * rows, 0);
    }
    BitDecoder decoder(payload.subspan(1 + channels));
    code_strip(planes, width, rows, choice, decoder);
    if (!decoder.consumed_exactly())
    {
        return std::unexpected(Error::corrupt_payload);
    }
    from_planes(planes, width * rows, channels, choice.transform, pixels);
    return {};
}

// ---------------------------------------------------------------------------------------------
// Lossy experiment (docs/benchmarks.md, "Image track: lossy experiment"): YCbCr at full
// resolution, 8x8 DCT, JPEG's quantisation tables scaled by a quality, coefficients coded by a
// context mixer.

constexpr std::array<uint8_t, 4> lossy_magic{0x89, 'I', 'N', 'L'};
constexpr uint8_t lossy_version = 1;
constexpr std::size_t lossy_header_bytes = 4 + 1 + 1 + 1 + 4 + 4;
constexpr std::size_t block = 8;

constexpr std::array<uint8_t, 64> zigzag{
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// JPEG's example tables (ITU T.81, Annex K), row-major.
constexpr std::array<uint8_t, 64> luma_table{
    16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
constexpr std::array<uint8_t, 64> chroma_table{
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
    99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

std::array<int, 64> quantisers(unsigned quality, bool chroma)
{
    const int scale =
        quality < 50 ? static_cast<int>(5000 / quality) : static_cast<int>(200 - (2 * quality));
    std::array<int, 64> q{};
    for (std::size_t i = 0; i < 64; ++i)
    {
        // Coarser chroma than JPEG's table, more so at high quality, where SSIMULACRA2 notices
        // chroma least relative to the bits it costs.
        const int chroma_percent = 120 + (9 * static_cast<int>(quality) / 10);
        const int base = chroma ? chroma_table[i] * chroma_percent / 100 : luma_table[i];
        q[i] = std::clamp(((base * scale) + 50) / 100, 1, 255);
    }
    return q;
}

const std::array<double, 64>& dct_basis()
{
    static const std::array<double, 64> basis = [] {
        std::array<double, 64> b{};
        for (std::size_t u = 0; u < block; ++u)
        {
            const double c = u == 0 ? std::sqrt(1.0 / block) : std::sqrt(2.0 / block);
            for (std::size_t x = 0; x < block; ++x)
            {
                b[(u * block) + x] =
                    c * std::cos((2.0 * static_cast<double>(x) + 1.0) * static_cast<double>(u) *
                                 std::numbers::pi / (2.0 * block));
            }
        }
        return b;
    }();
    return basis;
}

void forward_dct(std::array<double, 64>& data)
{
    const auto& b = dct_basis();
    std::array<double, 64> rows{};
    for (std::size_t y = 0; y < block; ++y)
    {
        for (std::size_t u = 0; u < block; ++u)
        {
            double sum = 0;
            for (std::size_t x = 0; x < block; ++x)
            {
                sum += b[(u * block) + x] * data[(y * block) + x];
            }
            rows[(y * block) + u] = sum;
        }
    }
    for (std::size_t u = 0; u < block; ++u)
    {
        for (std::size_t v = 0; v < block; ++v)
        {
            double sum = 0;
            for (std::size_t y = 0; y < block; ++y)
            {
                sum += b[(v * block) + y] * rows[(y * block) + u];
            }
            data[(v * block) + u] = sum;
        }
    }
}

void inverse_dct(std::array<double, 64>& data)
{
    const auto& b = dct_basis();
    std::array<double, 64> columns{};
    for (std::size_t u = 0; u < block; ++u)
    {
        for (std::size_t y = 0; y < block; ++y)
        {
            double sum = 0;
            for (std::size_t v = 0; v < block; ++v)
            {
                sum += b[(v * block) + y] * data[(v * block) + u];
            }
            columns[(y * block) + u] = sum;
        }
    }
    for (std::size_t y = 0; y < block; ++y)
    {
        for (std::size_t x = 0; x < block; ++x)
        {
            double sum = 0;
            for (std::size_t u = 0; u < block; ++u)
            {
                sum += b[(u * block) + x] * columns[(y * block) + u];
            }
            data[(y * block) + x] = sum;
        }
    }
}

// Smooths across block edges where the step is small enough to be a blocking artefact: the
// two pixels on each side move toward each other, by at most a fraction of the quantiser.
void deblock(std::vector<double>& plane, std::size_t width, std::size_t height,
             const std::array<int, 64>& q)
{
    const double limit = q[1] * 0.6;
    const auto filter = [&](double& p1, double& p0, double& q0, double& q1) {
        const double step = q0 - p0;
        if (std::abs(step) >= limit || std::abs(p1 - p0) >= limit / 2 ||
            std::abs(q1 - q0) >= limit / 2)
        {
            return;
        }
        const double delta = step / 4;
        p0 += delta;
        q0 -= delta;
        p1 += delta / 2;
        q1 -= delta / 2;
    };
    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = block; x < width; x += block)
        {
            double* row = plane.data() + (y * width);
            filter(row[x - 2], row[x - 1], row[x], row[x + 1]);
        }
    }
    for (std::size_t y = block; y < height; y += block)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            filter(plane[((y - 2) * width) + x], plane[((y - 1) * width) + x],
                   plane[(y * width) + x], plane[((y + 1) * width) + x]);
        }
    }
}

// The coefficient model: every decision mixes counters in a few contexts.
class CoefficientModel
{
public:
    static constexpr std::size_t kinds = 164; // see the decision ids below

    CoefficientModel()
        : position_(std::size_t{3} * 64 * 16 * kinds)
        , neighbours_(std::size_t{3} * 64 * 16 * kinds)
        , count_(std::size_t{3} * 64 * 8 * kinds)
        , left_(std::size_t{3} * 64 * 16 * kinds)
        , up_(std::size_t{3} * 64 * 16 * kinds)
        , luma_(std::size_t{3} * 64 * 16 * kinds)
        , mixer_(std::size_t{3} * 16 * 4)
        , apm_(std::size_t{3} * kinds * 4)
    {
    }

    // `k`: zigzag position (0 for DC and the count); `remaining`: nonzeros left to code,
    // bucketed; `around`: the neighbouring blocks' magnitude at `k`, bucketed (0-15).
    // `left`, `up`: those blocks' signed value at `k`; `luma`: for chroma, the first channel's
    // signed value at `k` in this block.
    void start(unsigned channel, unsigned k, unsigned remaining, unsigned around, int left = 0,
               int up = 0, int luma = 0)
    {
        const auto signed_bucket = [](int v) {
            const auto m = std::min(7U, bit_length(static_cast<unsigned>(std::abs(v))));
            return v < 0 ? 7 - m : 7 + m;
        };
        left_index_ = (((channel * 64) + k) * 16 + signed_bucket(left)) * kinds;
        up_index_ = (((channel * 64) + k) * 16 + signed_bucket(up)) * kinds;
        luma_index_ = (((channel * 64) + k) * 16 + signed_bucket(luma)) * kinds;
        channel_ = channel;
        position_index_ = (((channel * 64) + k) * 16 + std::min(remaining, 15U)) * kinds;
        neighbours_index_ = (((channel * 64) + k) * 16 + std::min(around, 15U)) * kinds;
        count_index_ = (((channel * 64) + k) * 8 + std::min(around / 2, 7U)) * kinds;
        mixer_base_ = (std::size_t{channel} * 16 + std::min(k, 15U)) * 4;
    }

    int p(std::size_t decision)
    {
        counters_ = {&position_[position_index_ + decision],
                     &neighbours_[neighbours_index_ + decision],
                     &count_[count_index_ + decision],
                     &left_[left_index_ + decision],
                     &up_[up_index_ + decision],
                     &luma_[luma_index_ + decision]};
        for (std::size_t i = 0; i < counters_.size(); ++i)
        {
            inputs_[i] = stretch(counters_[i]->p12());
        }
        inputs_[6] = 256;
        for (std::size_t i = 7; i < Mixer::inputs; ++i)
        {
            inputs_[i] = 0;
        }
        const std::size_t group = decision == 0 ? 0 : (decision == 1 ? 1 : (decision < 32 ? 2 : 3));
        const int mixed = mixer_.mix(inputs_, mixer_base_ + group);
        const int refined = apm_.p(mixed, ((channel_ * kinds) + decision) * 4 + group);
        return std::clamp((mixed + (3 * refined)) / 4, 1, 4095);
    }

    void update(unsigned bit)
    {
        for (Counter* counter : counters_)
        {
            counter->update(bit, direct_limit);
        }
        mixer_.update(inputs_, bit);
        apm_.update(bit);
    }

private:
    std::vector<Counter> position_;
    std::vector<Counter> neighbours_;
    std::vector<Counter> count_;
    std::vector<Counter> left_;
    std::vector<Counter> up_;
    std::vector<Counter> luma_;
    Mixer mixer_;
    Apm apm_;
    unsigned channel_ = 0;
    std::size_t left_index_ = 0;
    std::size_t up_index_ = 0;
    std::size_t luma_index_ = 0;
    std::size_t position_index_ = 0;
    std::size_t neighbours_index_ = 0;
    std::size_t count_index_ = 0;
    std::size_t mixer_base_ = 0;
    std::array<Counter*, 6> counters_{};
    std::array<int, Mixer::inputs> inputs_{};
};

// Decision ids: 0 zero, 1 sign, 2-17 the magnitude's bit length (unary), 32 + 8 * k + i its
// mantissa bit i with bit length k + 1 (k < 8); 100-163 the block's nonzero count, a binary tree.
unsigned bucket(int v)
{
    return std::min(15U, bit_length(static_cast<unsigned>(std::abs(v))) * 2);
}

template <class Coder>
void code_coefficients(std::vector<std::vector<int>>& levels, std::size_t blocks_x,
                       std::size_t blocks_y, Coder& coder)
{
    constexpr bool encoding = requires(Coder& c) { c.encode(0U, 0); };
    CoefficientModel model;
    const auto code = [&](std::size_t decision, unsigned bit) -> unsigned {
        const int p = model.p(decision);
        if constexpr (encoding)
        {
            coder.encode(bit, p);
        }
        else
        {
            bit = coder.decode(p);
        }
        model.update(bit);
        return bit;
    };
    const auto code_value = [&](int value) -> int {
        const unsigned zero = code(0, value == 0 ? 1U : 0U);
        if (zero != 0)
        {
            return 0;
        }
        const unsigned negative = code(1, value < 0 ? 1U : 0U);
        const auto magnitude = static_cast<unsigned>(std::abs(value));
        const unsigned length = encoding ? bit_length(magnitude) : 0;
        unsigned k = 0;
        while (k < 15 && code(2 + k, length > k + 1 ? 1U : 0U) != 0)
        {
            ++k;
        }
        unsigned result = 1;
        for (unsigned b = k; b-- > 0;)
        {
            result = (result << 1U) |
                     code(32 + (std::min(k, 7U) * 8) + std::min(b, 7U), (magnitude >> b) & 1U);
        }
        return negative != 0 ? -static_cast<int>(result) : static_cast<int>(result);
    };
    const std::size_t channels = levels.size();
    for (std::size_t by = 0; by < blocks_y; ++by)
    {
        for (std::size_t bx = 0; bx < blocks_x; ++bx)
        {
            const std::size_t b = (by * blocks_x) + bx;
            for (std::size_t c = 0; c < channels; ++c)
            {
                std::vector<int>& level = levels[c];
                int* coefficients = level.data() + (b * 64);
                const int* left = bx > 0 ? coefficients - 64 : nullptr;
                const int* up = by > 0 ? coefficients - (blocks_x * 64) : nullptr;
                const auto around = [&](std::size_t k) {
                    const int l = left != nullptr ? std::abs(left[zigzag[k]]) : 0;
                    const int u = up != nullptr ? std::abs(up[zigzag[k]]) : 0;
                    return bucket((l + u + 1) / 2);
                };
                const auto count_of = [](const int* block_levels) {
                    unsigned n = 0;
                    for (std::size_t k = 1; k < 64; ++k)
                    {
                        n += block_levels[zigzag[k]] != 0 ? 1U : 0U;
                    }
                    return n;
                };
                // DC: the residual of the median predictor over the neighbouring blocks' DCs.
                const int dc_left = left != nullptr ? left[0] : (up != nullptr ? up[0] : 0);
                const int dc_up = up != nullptr ? up[0] : dc_left;
                const int dc_corner = left != nullptr && up != nullptr
                                          ? (coefficients - (blocks_x * 64) - 64)[0]
                                          : dc_up;
                const int most = std::max(dc_left, dc_up);
                const int least = std::min(dc_left, dc_up);
                const int dc_prediction =
                    dc_corner >= most ? least
                                      : (dc_corner <= least ? most : dc_left + dc_up - dc_corner);
                model.start(static_cast<unsigned>(c), 0, 0,
                            bucket(std::abs(dc_left - dc_corner) + std::abs(dc_up - dc_corner)));
                const int dc = code_value(encoding ? coefficients[0] - dc_prediction : 0);
                coefficients[0] = dc + dc_prediction;

                // The AC count, as a 6-bit binary tree.
                const unsigned neighbour_count = ((left != nullptr ? count_of(left) : 0) +
                                                  (up != nullptr ? count_of(up) : 0) + 1) /
                                                 2;
                model.start(static_cast<unsigned>(c), 0, 0, std::min(15U, neighbour_count / 4));
                const unsigned count = encoding ? count_of(coefficients) : 0;
                unsigned node = 1;
                for (unsigned bit = 6; bit-- > 0;)
                {
                    node = (node << 1U) | code(100 + std::min(node, 63U), (count >> bit) & 1U);
                }
                unsigned remaining = node - 64;
                for (std::size_t k = 1; k < 64 && remaining > 0; ++k)
                {
                    const std::size_t at = zigzag[k];
                    model.start(static_cast<unsigned>(c), static_cast<unsigned>(k),
                                std::min(remaining, 15U), around(k), left != nullptr ? left[at] : 0,
                                up != nullptr ? up[at] : 0, c > 0 ? levels[0][(b * 64) + at] : 0);
                    const int value = code_value(encoding ? coefficients[zigzag[k]] : 0);
                    coefficients[zigzag[k]] = value;
                    remaining -= value != 0 ? 1U : 0U;
                }
            }
        }
    }
}

} // namespace

bool is_lossy_image_file(std::span<const uint8_t> bytes)
{
    return bytes.size() >= lossy_magic.size() &&
           std::equal(lossy_magic.begin(), lossy_magic.end(), bytes.begin());
}

std::expected<std::vector<uint8_t>, Error> encode_lossy_image(const Image& image, unsigned quality)
{
    if ((image.channels != 1 && image.channels != 3) || quality < 1 || quality > 100)
    {
        return std::unexpected(Error::not_image);
    }
    const std::size_t width = image.width;
    const std::size_t height = image.height;
    const std::size_t channels = image.channels;
    const std::size_t blocks_x = (width + block - 1) / block;
    const std::size_t blocks_y = (height + block - 1) / block;
    std::vector<std::vector<int>> levels(channels, std::vector<int>(blocks_x * blocks_y * 64, 0));
    const std::array<std::array<int, 64>, 2> tables{quantisers(quality, false),
                                                    quantisers(quality, true)};
    for (std::size_t by = 0; by < blocks_y; ++by)
    {
        for (std::size_t bx = 0; bx < blocks_x; ++bx)
        {
            std::array<std::array<double, 64>, 3> data{};
            for (std::size_t y = 0; y < block; ++y)
            {
                for (std::size_t x = 0; x < block; ++x)
                {
                    const std::size_t px = std::min(width - 1, (bx * block) + x);
                    const std::size_t py = std::min(height - 1, (by * block) + y);
                    const uint8_t* pixel = image.pixels.data() + (((py * width) + px) * channels);
                    if (channels == 1)
                    {
                        data[0][(y * block) + x] = pixel[0] - 128.0;
                        continue;
                    }
                    const double r = pixel[0];
                    const double g = pixel[1];
                    const double b = pixel[2];
                    data[0][(y * block) + x] = (0.299 * r) + (0.587 * g) + (0.114 * b) - 128.0;
                    data[1][(y * block) + x] = (-0.168736 * r) - (0.331264 * g) + (0.5 * b);
                    data[2][(y * block) + x] = (0.5 * r) - (0.418688 * g) - (0.081312 * b);
                }
            }
            for (std::size_t c = 0; c < channels; ++c)
            {
                forward_dct(data[c]);
            }
            for (std::size_t c = 0; c < channels; ++c)
            {
                const std::array<int, 64>& q = tables[c == 0 ? 0 : 1];
                int* out = levels[c].data() + (((by * blocks_x) + bx) * 64);
                for (std::size_t i = 0; i < 64; ++i)
                {
                    const double scaled = std::abs(data[c][i]) / q[i];
                    // A dead zone: AC values round down a little, which saves more bits than it
                    // costs.
                    const double dead = i == 0 ? 0.0 : 0.1;
                    const auto magnitude = static_cast<int>(std::floor(scaled + 0.5 - dead));
                    out[i] = data[c][i] < 0 ? -std::max(0, magnitude) : std::max(0, magnitude);
                }
            }
        }
    }
    std::vector<uint8_t> out(lossy_magic.begin(), lossy_magic.end());
    out.push_back(lossy_version);
    out.push_back(static_cast<uint8_t>(channels));
    out.push_back(static_cast<uint8_t>(quality));
    put32(out, image.width);
    put32(out, image.height);
    BitEncoder encoder(out);
    code_coefficients(levels, blocks_x, blocks_y, encoder);
    encoder.finish();
    return out;
}

std::expected<Image, Error> decode_lossy_image(std::span<const uint8_t> bytes)
{
    if (!is_lossy_image_file(bytes))
    {
        return std::unexpected(Error::not_image);
    }
    if (bytes.size() < lossy_header_bytes)
    {
        return std::unexpected(Error::truncated);
    }
    if (bytes[4] != lossy_version)
    {
        return std::unexpected(Error::unsupported_version);
    }
    Image image;
    image.channels = bytes[5];
    const unsigned quality = bytes[6];
    image.width = get32(bytes, 7);
    image.height = get32(bytes, 11);
    const std::size_t width = image.width;
    const std::size_t height = image.height;
    const std::size_t channels = image.channels;
    if ((channels != 1 && channels != 3) || quality < 1 || quality > 100 || width == 0 ||
        height == 0 || width > max_image_side || height > max_image_side ||
        width * height > max_pixels_per_byte * bytes.size())
    {
        return std::unexpected(Error::corrupt_header);
    }
    const std::size_t blocks_x = (width + block - 1) / block;
    const std::size_t blocks_y = (height + block - 1) / block;
    std::vector<std::vector<int>> levels(channels, std::vector<int>(blocks_x * blocks_y * 64, 0));
    BitDecoder decoder(bytes.subspan(lossy_header_bytes));
    code_coefficients(levels, blocks_x, blocks_y, decoder);
    if (!decoder.consumed_exactly())
    {
        return std::unexpected(Error::corrupt_payload);
    }
    const std::array<std::array<int, 64>, 2> tables{quantisers(quality, false),
                                                    quantisers(quality, true)};
    const std::size_t padded_width = blocks_x * block;
    const std::size_t padded_height = blocks_y * block;
    std::vector<std::vector<double>> planes(channels,
                                            std::vector<double>(padded_width * padded_height));
    for (std::size_t by = 0; by < blocks_y; ++by)
    {
        for (std::size_t bx = 0; bx < blocks_x; ++bx)
        {
            for (std::size_t c = 0; c < channels; ++c)
            {
                const std::array<int, 64>& q = tables[c == 0 ? 0 : 1];
                const int* in = levels[c].data() + (((by * blocks_x) + bx) * 64);
                std::array<double, 64> data{};
                for (std::size_t i = 0; i < 64; ++i)
                {
                    data[i] = static_cast<double>(in[i]) * q[i];
                }
                inverse_dct(data);
                for (std::size_t y = 0; y < block; ++y)
                {
                    for (std::size_t x = 0; x < block; ++x)
                    {
                        planes[c][(((by * block) + y) * padded_width) + (bx * block) + x] =
                            data[(y * block) + x];
                    }
                }
            }
        }
    }
    for (std::size_t c = 0; c < channels; ++c)
    {
        deblock(planes[c], padded_width, padded_height, tables[c == 0 ? 0 : 1]);
    }
    image.pixels.resize(width * height * channels);
    const auto store = [](double v) {
        return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
    };
    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            uint8_t* pixel = image.pixels.data() + (((y * width) + x) * channels);
            const std::size_t at = (y * padded_width) + x;
            const double luma = planes[0][at] + 128.0;
            if (channels == 1)
            {
                pixel[0] = store(luma);
                continue;
            }
            const double cb = planes[1][at];
            const double cr = planes[2][at];
            pixel[0] = store(luma + (1.402 * cr));
            pixel[1] = store(luma - (0.344136 * cb) - (0.714136 * cr));
            pixel[2] = store(luma + (1.772 * cb));
        }
    }
    return image;
}

std::expected<Image, Error> read_pnm(std::istream& in)
{
    const std::string magic = pnm_token(in);
    Image image;
    if (magic == "P5" || magic == "P6")
    {
        const auto width = pnm_number(pnm_token(in));
        const auto height = pnm_number(pnm_token(in));
        const auto maximum = pnm_number(pnm_token(in));
        if (!width || !height || !maximum || *maximum != 255)
        {
            return std::unexpected(Error::not_image);
        }
        image.width = *width;
        image.height = *height;
        image.channels = magic == "P5" ? 1 : 3;
    }
    else if (magic == "P7")
    {
        std::optional<uint32_t> width;
        std::optional<uint32_t> height;
        std::optional<uint32_t> depth;
        std::optional<uint32_t> maximum;
        std::string type;
        while (true)
        {
            const std::string key = pnm_token(in);
            if (key.empty())
            {
                return std::unexpected(Error::not_image);
            }
            if (key == "ENDHDR")
            {
                break;
            }
            const std::string value = pnm_token(in);
            if (key == "WIDTH")
            {
                width = pnm_number(value);
            }
            else if (key == "HEIGHT")
            {
                height = pnm_number(value);
            }
            else if (key == "DEPTH")
            {
                depth = pnm_number(value);
            }
            else if (key == "MAXVAL")
            {
                maximum = pnm_number(value);
            }
            else if (key == "TUPLTYPE")
            {
                type = value;
            }
        }
        const bool known = type.empty() || type == "GRAYSCALE" || type == "GRAYSCALE_ALPHA" ||
                           type == "RGB" || type == "RGB_ALPHA";
        if (!width || !height || !depth || !maximum || *maximum != 255 || *depth < 1 ||
            *depth > 4 || !known)
        {
            return std::unexpected(Error::not_image);
        }
        image.width = *width;
        image.height = *height;
        image.channels = static_cast<uint8_t>(*depth);
    }
    else
    {
        return std::unexpected(Error::not_image);
    }
    if (image.width == 0 || image.height == 0 || image.width > max_image_side ||
        image.height > max_image_side)
    {
        return std::unexpected(Error::not_image);
    }
    image.pixels.resize(std::size_t{image.width} * image.height * image.channels);
    in.read(reinterpret_cast<char*>(image.pixels.data()),
            static_cast<std::streamsize>(image.pixels.size()));
    if (std::cmp_not_equal(in.gcount(), image.pixels.size()))
    {
        return std::unexpected(in.bad() ? Error::read_failed : Error::truncated);
    }
    return image;
}

void write_pnm(std::ostream& out, const Image& image)
{
    if (image.channels == 1 || image.channels == 3)
    {
        out << (image.channels == 1 ? "P5\n" : "P6\n") << image.width << ' ' << image.height
            << "\n255\n";
    }
    else
    {
        out << "P7\nWIDTH " << image.width << "\nHEIGHT " << image.height << "\nDEPTH "
            << unsigned{image.channels} << "\nMAXVAL 255\nTUPLTYPE "
            << (image.channels == 2 ? "GRAYSCALE_ALPHA" : "RGB_ALPHA") << "\nENDHDR\n";
    }
    out.write(reinterpret_cast<const char*>(image.pixels.data()),
              static_cast<std::streamsize>(image.pixels.size()));
}

bool is_image_file(std::span<const uint8_t> bytes)
{
    return bytes.size() >= image_magic.size() &&
           std::equal(image_magic.begin(), image_magic.end(), bytes.begin());
}

std::vector<uint8_t> encode_image(const Image& image, unsigned threads)
{
    const std::size_t width = image.width;
    const std::size_t height = image.height;
    const unsigned channels = image.channels;
    const auto strip_rows =
        static_cast<uint32_t>(std::clamp<std::size_t>(strip_pixels / width, 1, height));
    const std::size_t strips = (height + strip_rows - 1) / strip_rows;

    std::vector<std::vector<uint8_t>> coded(strips);
    {
        ThreadPool pool(std::max(1U, std::min<unsigned>(threads, static_cast<unsigned>(strips))));
        std::vector<std::future<void>> done;
        done.reserve(strips);
        for (std::size_t s = 0; s < strips; ++s)
        {
            done.push_back(pool.submit([&, s](std::size_t) {
                const std::size_t first = s * strip_rows;
                const std::size_t rows = std::min<std::size_t>(strip_rows, height - first);
                coded[s] =
                    encode_strip(std::span(image.pixels)
                                     .subspan(first * width * channels, rows * width * channels),
                                 width, rows, channels);
            }));
        }
        for (auto& future : done)
        {
            future.get();
        }
    }

    std::vector<uint8_t> out(image_magic.begin(), image_magic.end());
    out.push_back(image_version);
    out.push_back(static_cast<uint8_t>(channels));
    out.push_back(8);
    put32(out, image.width);
    put32(out, image.height);
    put32(out, strip_rows);
    put32(out, crc32(image.pixels));
    for (const std::vector<uint8_t>& strip : coded)
    {
        put32(out, static_cast<uint32_t>(strip.size()));
        out.insert(out.end(), strip.begin(), strip.end());
    }
    return out;
}

std::expected<Image, Error> decode_image(std::span<const uint8_t> bytes, unsigned threads)
{
    if (!is_image_file(bytes))
    {
        return std::unexpected(Error::not_image);
    }
    if (bytes.size() < header_bytes)
    {
        return std::unexpected(Error::truncated);
    }
    if (bytes[4] != image_version)
    {
        return std::unexpected(Error::unsupported_version);
    }
    Image image;
    image.channels = bytes[5];
    image.width = get32(bytes, 7);
    image.height = get32(bytes, 11);
    const uint32_t strip_rows = get32(bytes, 15);
    const uint32_t crc = get32(bytes, 19);
    if (image.channels < 1 || image.channels > 4 || bytes[6] != 8 || image.width == 0 ||
        image.height == 0 || image.width > max_image_side || image.height > max_image_side ||
        strip_rows == 0 || strip_rows > image.height)
    {
        return std::unexpected(Error::corrupt_header);
    }
    const std::size_t width = image.width;
    const std::size_t height = image.height;
    const unsigned channels = image.channels;
    const std::size_t strips = (height + strip_rows - 1) / strip_rows;

    std::vector<std::span<const uint8_t>> payloads;
    std::size_t at = header_bytes;
    for (std::size_t s = 0; s < strips; ++s)
    {
        if (bytes.size() - at < 4)
        {
            return std::unexpected(Error::truncated);
        }
        const uint32_t size = get32(bytes, at);
        at += 4;
        if (bytes.size() - at < size)
        {
            return std::unexpected(Error::truncated);
        }
        payloads.push_back(bytes.subspan(at, size));
        at += size;
    }
    // Every pixel costs at least one coded decision of at least log2(4096 / 4095) bits, so a real
    // file has under 22 700 pixels per byte: a header claiming more is rejected before allocating.
    if (at != bytes.size() || width * height > max_pixels_per_byte * bytes.size())
    {
        return std::unexpected(Error::corrupt_header);
    }

    image.pixels.resize(width * height * channels);
    std::vector<std::expected<void, Error>> results(strips);
    {
        ThreadPool pool(std::max(1U, std::min<unsigned>(threads, static_cast<unsigned>(strips))));
        std::vector<std::future<void>> done;
        done.reserve(strips);
        for (std::size_t s = 0; s < strips; ++s)
        {
            done.push_back(pool.submit([&, s](std::size_t) {
                const std::size_t first = s * strip_rows;
                const std::size_t rows = std::min<std::size_t>(strip_rows, height - first);
                results[s] =
                    decode_strip(payloads[s], width, rows, channels,
                                 std::span(image.pixels)
                                     .subspan(first * width * channels, rows * width * channels));
            }));
        }
        for (auto& future : done)
        {
            future.get();
        }
    }
    for (const auto& result : results)
    {
        if (!result)
        {
            return std::unexpected(result.error());
        }
    }
    if (crc32(image.pixels) != crc)
    {
        return std::unexpected(Error::checksum_mismatch);
    }
    return image;
}

} // namespace compressor
