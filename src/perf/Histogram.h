#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace f4cf::perf
{
    /**
     * Lock-free duration histogram behind every perf site: record from any thread, peek at it or drain it into a
     * Snapshot, and read percentiles from snapshots, which merge by plain addition.
     *
     * Buckets are log-linear over nanoseconds (HdrHistogram style): every power of two is split into SUB_BUCKETS
     * equal buckets, so a bucket is at most 1/16 of its lower bound wide, and a percentile, reported as the midpoint
     * of its bucket, is within 1/32 (~3%) of the exact one. Below 2^LOWEST_EXPONENT ns (~1 us) the buckets are a flat
     * 64 ns wide; from 2^HIGHEST_EXPONENT ns (~1.07 s) up everything shares one overflow bucket, whose percentiles
     * read as the max. Count, sum, min and max are exact.
     *
     * Recording is a bit scan, two relaxed atomic adds and two relaxed loads (plus a CAS on a new min or max): no
     * locks and no allocations, on any thread. A live histogram is ~1.4 KB and a Snapshot ~2.7 KB.
     *
     * Plain std only - no game or framework headers - so it builds in a test target.
     */
    class Histogram
    {
    public:
        static constexpr unsigned SUB_BUCKET_BITS = 4;
        static constexpr std::size_t SUB_BUCKETS = std::size_t{ 1 } << SUB_BUCKET_BITS;
        // below 2^10 ns the buckets are a flat 2^(10-4) = 64 ns wide
        static constexpr unsigned LOWEST_EXPONENT = 10;
        // 2^30 ns and above share the overflow bucket
        static constexpr unsigned HIGHEST_EXPONENT = 30;
        static constexpr std::size_t OVERFLOW_BUCKET = (HIGHEST_EXPONENT - LOWEST_EXPONENT + 1) * SUB_BUCKETS;
        static constexpr std::size_t BUCKET_COUNT = OVERFLOW_BUCKET + 1;

        /**
         * The statistics of a snapshot, in milliseconds.
         */
        struct Summary
        {
            std::uint64_t count = 0;
            double totalMs = 0;
            double avgMs = 0;
            double minMs = 0;
            double maxMs = 0;
            double p50Ms = 0;
            double p95Ms = 0;
            double p99Ms = 0;
        };

        /**
         * A histogram's samples at rest: what a drain hands over, and what windows add up into. NOT thread-safe.
         */
        struct Snapshot
        {
            std::array<std::uint64_t, BUCKET_COUNT> buckets{};
            std::uint64_t count = 0;
            std::uint64_t sumNs = 0;
            // both 0 while count is 0
            std::uint64_t minNs = 0;
            std::uint64_t maxNs = 0;

            /**
             * Add another snapshot's samples, as if they had been recorded here.
             */
            void merge(const Snapshot& other)
            {
                if (other.count == 0) {
                    return;
                }
                for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
                    buckets[i] += other.buckets[i];
                }
                minNs = count == 0 ? other.minNs : (std::min)(minNs, other.minNs);
                maxNs = (std::max)(maxNs, other.maxNs);
                count += other.count;
                sumNs += other.sumNs;
            }

            /**
             * Nearest-rank percentile (0-100) in nanoseconds: the midpoint of the bucket holding that rank, kept within
             * [min, max]. The lowest rank is the exact min and the highest the exact max. 0 while empty.
             */
            [[nodiscard]] std::uint64_t percentileNs(const double percentile) const
            {
                if (count == 0) {
                    return 0;
                }
                const auto wanted = static_cast<std::uint64_t>(std::ceil(percentile / 100.0 * static_cast<double>(count)));
                const auto rank = (std::min)((std::max)(wanted, std::uint64_t{ 1 }), count);
                if (rank == 1) {
                    return minNs;
                }
                if (rank == count) {
                    return maxNs;
                }
                std::uint64_t seen = 0;
                for (std::size_t i = 0; i < OVERFLOW_BUCKET; ++i) {
                    seen += buckets[i];
                    if (seen >= rank) {
                        const auto lower = bucketLowerBound(i);
                        const auto midpoint = lower + (bucketUpperBound(i) - lower) / 2;
                        return (std::min)((std::max)(midpoint, minNs), maxNs);
                    }
                }
                return maxNs;
            }

            [[nodiscard]] Summary summary() const
            {
                Summary s;
                if (count == 0) {
                    return s;
                }
                s.count = count;
                s.totalMs = toMs(sumNs);
                s.avgMs = s.totalMs / static_cast<double>(count);
                s.minMs = toMs(minNs);
                s.maxMs = toMs(maxNs);
                s.p50Ms = toMs(percentileNs(50.0));
                s.p95Ms = toMs(percentileNs(95.0));
                s.p99Ms = toMs(percentileNs(99.0));
                return s;
            }

            static double toMs(const std::uint64_t ns)
            {
                return static_cast<double>(ns) / 1'000'000.0;
            }
        };

        /**
         * The bucket a duration in nanoseconds falls in.
         */
        static constexpr std::size_t bucketIndex(const std::uint64_t ns)
        {
            if (ns < (std::uint64_t{ 1 } << LOWEST_EXPONENT)) {
                return static_cast<std::size_t>(ns >> (LOWEST_EXPONENT - SUB_BUCKET_BITS));
            }
            if (ns >= (std::uint64_t{ 1 } << HIGHEST_EXPONENT)) {
                return OVERFLOW_BUCKET;
            }
            // ns >> (exponent - SUB_BUCKET_BITS) keeps the top SUB_BUCKET_BITS + 1 bits: SUB_BUCKETS to 2 * SUB_BUCKETS - 1
            const auto exponent = static_cast<unsigned>(std::bit_width(ns)) - 1;
            const auto octave = static_cast<std::size_t>(exponent - LOWEST_EXPONENT + 1);
            const auto sub = static_cast<std::size_t>(ns >> (exponent - SUB_BUCKET_BITS)) - SUB_BUCKETS;
            return octave * SUB_BUCKETS + sub;
        }

        /**
         * The smallest duration in a bucket, in nanoseconds.
         */
        static constexpr std::uint64_t bucketLowerBound(const std::size_t index)
        {
            const auto octave = index / SUB_BUCKETS;
            const auto sub = static_cast<std::uint64_t>(index % SUB_BUCKETS);
            if (octave == 0) {
                return sub << (LOWEST_EXPONENT - SUB_BUCKET_BITS);
            }
            const auto exponent = LOWEST_EXPONENT + static_cast<unsigned>(octave) - 1;
            return (SUB_BUCKETS + sub) << (exponent - SUB_BUCKET_BITS);
        }

        /**
         * The first duration past a bucket, in nanoseconds; the overflow bucket has no end.
         */
        static constexpr std::uint64_t bucketUpperBound(const std::size_t index)
        {
            return index >= OVERFLOW_BUCKET ? (std::numeric_limits<std::uint64_t>::max)() : bucketLowerBound(index + 1);
        }

        void record(const std::chrono::nanoseconds duration)
        {
            recordNs(static_cast<std::uint64_t>((std::max)(duration.count(), std::int64_t{ 0 })));
        }

        void recordNs(const std::uint64_t ns)
        {
            _buckets[bucketIndex(ns)].fetch_add(1, std::memory_order_relaxed);
            _sumNs.fetch_add(ns, std::memory_order_relaxed);
            lowerTo(_minNs, ns);
            raiseTo(_maxNs, ns);
        }

        /**
         * Hand over everything recorded since the last drain and start empty. Any thread, and safe while others record.
         *
         * Each value is exchanged for its empty state one at a time, so a sample recorded during the drain can have
         * its bucket in one window and its sum, min or max in the next. The count comes from the buckets, so it
         * always agrees with the percentiles, and min and max are widened to the buckets the window holds, so a
         * split sample can't leave them outside what was counted.
         */
        [[nodiscard]] Snapshot drain()
        {
            return collect(*this, [](auto& value, const auto empty) {
                return value.exchange(empty, std::memory_order_relaxed);
            });
        }

        /**
         * Everything recorded since the last drain, leaving it in place. Any thread, and safe while others record; a
         * sample recorded during the read may be only partly in it, as with drain.
         */
        [[nodiscard]] Snapshot peek() const
        {
            return collect(*this, [](const auto& value, auto) {
                return value.load(std::memory_order_relaxed);
            });
        }

    private:
        static constexpr std::uint64_t EMPTY_MIN = (std::numeric_limits<std::uint64_t>::max)();

        /**
         * The body of drain and peek: take(atomic, emptyValue) returns the atomic's value, and drain also resets it.
         */
        template <typename Self, typename Take>
        static Snapshot collect(Self& self, Take take)
        {
            Snapshot snapshot;
            std::size_t firstUsed = BUCKET_COUNT;
            std::size_t lastUsed = 0;
            for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
                const std::uint64_t n = take(self._buckets[i], std::uint32_t{ 0 });
                snapshot.buckets[i] = n;
                snapshot.count += n;
                if (n > 0) {
                    firstUsed = (std::min)(firstUsed, i);
                    lastUsed = i;
                }
            }
            snapshot.sumNs = take(self._sumNs, std::uint64_t{ 0 });
            const std::uint64_t minNs = take(self._minNs, EMPTY_MIN);
            const std::uint64_t maxNs = take(self._maxNs, std::uint64_t{ 0 });
            if (snapshot.count == 0) {
                snapshot.sumNs = 0;
                return snapshot;
            }
            snapshot.maxNs = (std::max)(maxNs, bucketLowerBound(lastUsed));
            snapshot.minNs = (std::min)((std::min)(minNs, bucketUpperBound(firstUsed) - 1), snapshot.maxNs);
            return snapshot;
        }

        static void lowerTo(std::atomic<std::uint64_t>& target, const std::uint64_t value)
        {
            auto current = target.load(std::memory_order_relaxed);
            while (value < current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
            }
        }

        static void raiseTo(std::atomic<std::uint64_t>& target, const std::uint64_t value)
        {
            auto current = target.load(std::memory_order_relaxed);
            while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
            }
        }

        // 32 bits per bucket: a window is drained long before any bucket could wrap
        std::array<std::atomic<std::uint32_t>, BUCKET_COUNT> _buckets{};
        std::atomic<std::uint64_t> _sumNs{ 0 };
        std::atomic<std::uint64_t> _minNs{ EMPTY_MIN };
        std::atomic<std::uint64_t> _maxNs{ 0 };
    };
}
