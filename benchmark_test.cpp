#include <memory>
#include <algorithm>
#include <random>
#include <benchmark/benchmark.h>

#include "timer_wheel.h"

const int MaxN = 50000;   // max timer count

// see https://en.wikipedia.org/wiki/Linear_congruential_generator
uint32_t lcg_seed(uint32_t seed) {
    return seed * 214013 + 2531011;
}

uint32_t lcg_rand(uint32_t& seed) {
    seed = seed * 214013 + 2531011;
    uint32_t r = uint32_t(seed >> 16) & 0x7fff;
    return r;
}

static std::shared_ptr<timer::timer_wheel<1>> add_timer(benchmark::State& state) {
    uint32_t seed = lcg_seed(12345);

    auto tw = std::make_shared<timer::timer_wheel<1>>();
    auto dummy = [](timer::timer_handle) { };
    for (auto _ : state)
    {
        uint32_t duration = lcg_rand(seed) % 5000;
        tw->add(std::chrono::milliseconds(duration), dummy);
    }
    return tw;
}

static void BM_add_timer(benchmark::State& state) {
    auto timer = add_timer(state);
    benchmark::DoNotOptimize(timer);
}

static std::shared_ptr<timer::timer_wheel<1>> add_timer(int N, std::vector<timer::timer_handle>& out) {
    uint32_t seed = lcg_seed(12345);
    auto tw = std::make_shared<timer::timer_wheel<1>>();
    auto dummy = [](timer::timer_handle) { };
    for (int i = 0; i < N; i++)
    {
        uint32_t duration = lcg_rand(seed) % 5000;
        auto tid = tw->add(std::chrono::milliseconds(duration), dummy);
        out.push_back(tid);
    }

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(out.begin(), out.end(), g);
    return tw;
}

static void stop_timer(benchmark::State& state) {
    int N = (int)state.max_iterations;
    std::vector<timer::timer_handle> timer_ids;
    timer_ids.reserve(N);
    auto timer = add_timer(N, timer_ids);
    for (auto _ : state)
    {
        if (timer_ids.empty()) {
            break;
        }
        auto timer_id = timer_ids.back();
        timer_ids.pop_back();
        timer->stop(timer_id);
    }
    benchmark::DoNotOptimize(timer);
}

static void BM_stop_timer(benchmark::State& state) {

    stop_timer(state);
}

static void tick_timer(benchmark::State& state) {
    std::vector<timer::timer_handle> timer_ids;
    timer_ids.reserve(MaxN);
    auto timer = add_timer(MaxN, timer_ids);
    for (auto _ : state)
    {
        timer->execute();
    }
    benchmark::DoNotOptimize(timer);
}

static void BM_tick_timer(benchmark::State& state) {

    tick_timer(state);
}

BENCHMARK(BM_add_timer);
BENCHMARK(BM_stop_timer);
BENCHMARK(BM_tick_timer);