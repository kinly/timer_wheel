#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <mutex>
#include <algorithm>

namespace base {
    /**
     * \brief 时间轮定时器
     * 终点时间：2100-01-01 00:00:00 (period: 1ms) see: clock
     * min period: 1ms
     */

    using time_clock = std::chrono::system_clock;
    using time_duration = std::chrono::milliseconds;
    using timer_handle = uint64_t;
    using timer_callback = std::function<void(timer_handle)>;

    class non_lock {
    public:
        void lock() {}
        void unlock() {}
    };

    template<uint64_t _Precision = 10, class _Mutex = non_lock>
    class timer_wheel {
        using time64_t = uint64_t;
        using bucket_t = time64_t;

        struct clock {

            time64_t _time64 = 0;

            clock(time64_t src)
                : _time64(src) {
            }

            static constexpr bucket_t _5_bits = 10;
            static constexpr bucket_t _4_bits = 8;
            static constexpr bucket_t _3_bits = 6;
            static constexpr bucket_t _2_bits = 6;
            static constexpr bucket_t _1_bits = 6;
            static constexpr bucket_t _0_bits = 6;

            static constexpr bucket_t _5_edge = 1ull << _5_bits;
            static constexpr bucket_t _4_edge = 1ull << _4_bits;
            static constexpr bucket_t _3_edge = 1ull << _3_bits;
            static constexpr bucket_t _2_edge = 1ull << _2_bits;
            static constexpr bucket_t _1_edge = 1ull << _1_bits;
            static constexpr bucket_t _0_edge = 1ull << _0_bits;

            // https://stackoverflow.com/questions/76605488/inconsistent-results-when-type-punning-uint64-t-with-union-and-bit-field
            constexpr bucket_t _5() const {
                return (_time64 >> 0) & (_5_edge - 1);
            }
            constexpr bucket_t _4() const {
                return (_time64 >> _5_bits) & (_4_edge - 1);
            }
            constexpr bucket_t _3() const {
                return (_time64 >> (_4_bits + _5_bits)) & (_3_edge - 1);
            }
            constexpr bucket_t _2() const {
                return (_time64 >> (_3_bits + _4_bits + _5_bits)) & (_2_edge - 1);
            }
            constexpr bucket_t _1() const {
                return (_time64 >> (_2_bits + _3_bits + _4_bits + _5_bits)) & (_1_edge - 1);
            }
            constexpr bucket_t _0() const {
                return (_time64 >> (_1_bits + _2_bits + _3_bits + _4_bits + _5_bits)) & (_0_edge - 1);
            }
        };

        static constexpr std::size_t bucket_count = 0x600; // 分桶总数

        struct handle_gen {
        private:
            std::mutex _mutex;
            uint16_t _crc = 0;
            std::queue<timer_handle> _free_ids;

            static timer_handle next() {
                static std::atomic<timer_handle> next_ = 0;
                if (++next_ == invaild_next) {
                    next_ = 1;  // warning....
                }
                return next_;
            }
        public:
            handle_gen() = default;
            ~handle_gen() = default;

            static handle_gen& instance() {
                static handle_gen inst;
                return inst;
            }

            static constexpr timer_handle invaild_next = 0xFFFFFFFFull;
            static constexpr timer_handle invaild_handle = 0x7FFFFFFFFFull;

            timer_handle get() noexcept {
                auto make = [](timer_handle handle_, uint16_t& crc) -> timer_handle {
                    return handle_ & invaild_next | ((++crc & 0x7Full) << 32);
                };

                std::unique_lock<std::mutex> lock(_mutex);    // todo: 待优化
                if (_free_ids.empty()) {
                    static uint16_t default_crc = 1;
                    return make(next(), default_crc);
                }
                auto result = _free_ids.front();
                _free_ids.pop();
                return make(result, _crc);
            }

            void put(timer_handle handle_) noexcept {
                std::unique_lock<std::mutex> lock(_mutex);    // todo: 待优化
                _free_ids.push(handle_ & invaild_next);
            }
        };

        struct event {
            timer_handle _handle = handle_gen::invaild_handle;  // 句柄
            time64_t _next = 0;                                 // 下次执行时间
            time64_t _period = 0;                               // 间隔时间
            uint64_t _round = 1;                                // 执行轮次（剩余）
            timer_callback _callback = nullptr;                 // 回调

            explicit event(time64_t nxt, time64_t period, uint64_t round, timer_callback&& cb)
                : _next(nxt)
                , _period(period)
                , _round(round)
                , _callback(cb) {
                _handle = handle_gen::instance().get();
                if (_period == 0) 
                    _round = 1;
            }
            ~event() {
                if (_handle == handle_gen::invaild_handle)
                    return;
                handle_gen::instance().put(_handle);
            }
        };

        constexpr time64_t tick() const {
            return std::chrono::duration_cast<time_duration>(time_clock::now().time_since_epoch()).count();
        }

    private:
        _Mutex _mutex;
        static constexpr time64_t _precision = _Precision;   // 精度

        std::vector<std::list<time64_t>> _wheels;            // 时间轮
        std::unordered_map<timer_handle, std::shared_ptr<event>> _events;

        time64_t _tick = tick() / _precision;                // 扳手时钟

    public:
        timer_wheel() {
            std::unique_lock<_Mutex> lock(_mutex);
            _wheels.resize(bucket_count);
        }

        template <class Rep, class Period>
        inline timer_handle add(const std::chrono::duration<Rep, Period>& when, timer_callback&& callback, const time_duration& period = time_duration::zero(), const int64_t round = 0) {

            std::shared_ptr<event> event_ = std::make_shared<event>(
                std::chrono::duration_cast<time_duration>((time_clock::now() + when).time_since_epoch()).count() / _precision,
                period.count(),
                round,
                std::forward<timer_callback>(callback)
            );

            std::unique_lock<_Mutex> lock(_mutex);
            _events.emplace(event_->_handle, event_);
            submit(event_);
            return event_->_handle;
        }

        inline time_duration stop(const timer_handle& handle) {
            std::unique_lock<_Mutex> lock(_mutex);
            const auto iter = _events.find(handle);
            if (iter == _events.end() || iter->second == nullptr)
                return time_duration(0);
            auto evt = iter->second;
            _events.erase(iter);
            const auto tick_ = tick();
            const auto next_ = evt->_next * _precision;
            if (next_ >= tick_) 
                return time_duration(next_ - tick_);
            return time_duration(0);
        }

        inline void execute() {
            const auto tick_now = tick() / _precision;

            while (_tick <= tick_now)
            {
                clock clk = { _tick };

                if (clk._5()) {
                    step_list(_wheels[clk._5()]);
                } else if (clk._4()) {
                    step_list(_wheels[clk._4() + clock::_5_edge]);
                } else if (clk._3()) {
                    step_list(_wheels[clk._3() + clock::_4_edge + clock::_5_edge]);
                } else if (clk._2()) {
                    step_list(_wheels[clk._2() + clock::_3_edge + clock::_4_edge + clock::_5_edge]);
                } else if (clk._1()) {
                    step_list(_wheels[clk._1() + clock::_2_edge + clock::_3_edge + clock::_4_edge + clock::_5_edge]);
                } else if (clk._0()) {
                    step_list(_wheels[clk._0() + clock::_1_edge + clock::_2_edge + clock::_3_edge + clock::_4_edge + clock::_5_edge]);
                }

                _tick += 1;
            }
        }

    private:
        inline void submit(std::shared_ptr<event> evt) {
            if (nullptr == evt)
                return;

            clock clk1 = { evt->_next };
            clock clk2 = { _tick };

            if (clk1._0() != clk2._0()) {
                _wheels[clock::_5_edge + clock::_4_edge + clock::_3_edge + clock::_2_edge + clock::_1_edge + clk1._0()].push_back(evt->_handle);
            } else if (clk1._1() != clk2._1()) {
                _wheels[clock::_5_edge + clock::_4_edge + clock::_3_edge + clock::_2_edge + clk1._1()].push_back(evt->_handle);
            } else if (clk1._2() != clk2._2()) {
                _wheels[clock::_5_edge + clock::_4_edge + clock::_3_edge + clk1._2()].push_back(evt->_handle);
            } else if (clk1._3() != clk2._3()) {
                _wheels[clock::_5_edge + clock::_4_edge + clk1._3()].push_back(evt->_handle);
            } else if (clk1._4() != clk2._4()) {
                _wheels[clock::_5_edge + clk1._4()].push_back(evt->_handle);
            } else {
                _wheels[clk1._5()].push_back(evt->_handle);
            }
        }

        inline void step_list(std::list<timer_handle>& lst) {
            std::unique_lock<_Mutex> lock(_mutex);

            auto iter = lst.begin();
            while (iter != lst.end())
            {
                timer_handle handle = *iter;
                ++iter;

                const auto iter_evt = _events.find(handle);
                if (iter_evt == _events.end())
                    continue;

                std::shared_ptr<event> evt = iter_evt->second;

                if (evt && evt->_next == _tick)
                {
                    if (evt->_round) {
                        if (evt->_callback)
                            evt->_callback(handle);
                        evt->_round -= 1;
                    }
                    
                    if (evt->_round == 0ull) {
                        _events.erase(iter_evt);
                        continue;
                    }
                    evt->_next = (tick() + evt->_period) / _precision;
                }
                submit(evt);
            }

            lst.clear();
        }
    };

}; // end namespace base


/*
 *    {
        base::timer_wheel<10> tw;
        tw.add(std::chrono::milliseconds(1000), [](base::timer_handle time_h) {
            std::cout << "tick....." << time_h << std::endl;
        }, std::chrono::milliseconds(100), 10);

        uint32_t count = 0;
        tw.add(std::chrono::milliseconds(50), [&count, &tw](base::timer_handle time_h) {
            std::cout << "20...tick....." << time_h << std::endl;
            count += 1;
            if (count >= 10)
                tw.stop(time_h);
        }, std::chrono::milliseconds(20), -1);

        while (true) {
            tw.execute();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            //std::cout << "................while....." << std::endl;
        }
    }
 *
 */
