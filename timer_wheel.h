#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>

#include "crontab.h"

namespace timer {
/**
 * \brief 时间轮定时器
 * 终点时间：2100-01-01 00:00:00 (period: 1ms) see: clock
 * min period: 1ms
 */

using timestamp = uint64_t;
using time_clock = std::chrono::system_clock;
using time_duration = std::chrono::milliseconds;
using timer_handle = uint64_t;

class empty_mutex {
 public:
  void lock() {}
  void unlock() {}
};

template <typename duration_tt = std::chrono::milliseconds>
static constexpr time_clock::time_point __time_point(
    long long value = 0) noexcept {
  return time_clock::now() + duration_tt(value);
}

template <typename duration_tt = std::chrono::milliseconds>
static constexpr timestamp current_timestamp() noexcept {
  return std::chrono::duration_cast<duration_tt>(
             __time_point<duration_tt>().time_since_epoch())
      .count();
}

template <typename duration_tt = std::chrono::milliseconds,
          typename req_tt = long long, typename period_tt = std::milli>
static constexpr timestamp relative_timestamp(
    std::chrono::duration<req_tt, period_tt> value) noexcept {
  return std::chrono::duration_cast<duration_tt>(
             __time_point<std::chrono::duration<req_tt, period_tt>>(
                 value.count())
                 .time_since_epoch())
      .count();
}

// todo: 性能较差
// static std::string current_timestamp_str() {
//     auto time_point_ = __time_point<time_duration>();
//     std::time_t tt = time_clock::to_time_t(time_point_);
//     std::stringstream ss;
//     ss << std::put_time(std::localtime(&tt), "%Y-%m-%d %X");
//     ss << "." << time_point_.time_since_epoch().count() % 1000;
//     return ss.str();
// }
using time64_t = uint64_t;
using bucket_t = time64_t;

static constexpr time64_t tick() noexcept {
  return current_timestamp<time_duration>();
}

struct clock {
  time64_t _time64 = 0;

  constexpr clock(time64_t src) : _time64(src) {}
  constexpr clock() : clock(0) {}

  // static constexpr bucket_t _5_bits = 10;
  // static constexpr bucket_t _4_bits = 8;
  // static constexpr bucket_t _3_bits = 6;
  // static constexpr bucket_t _2_bits = 6;
  // static constexpr bucket_t _1_bits = 6;
  // static constexpr bucket_t _0_bits = 6;

  static constexpr bucket_t _5_bits = 4;  // 1 -> 16
  static constexpr bucket_t _4_bits = 2;  // 16 -> 64
  static constexpr bucket_t _3_bits = 2;
  static constexpr bucket_t _2_bits = 2;
  static constexpr bucket_t _1_bits = 10;
  static constexpr bucket_t _0_bits = 10;

  static constexpr bucket_t _5_edge = 1ull << _5_bits;  // 1ms -> 1024ms(1s)
  static constexpr bucket_t _4_edge = 1ull
                                      << _4_bits;  // 1024ms -> 262144ms(4min)
  static constexpr bucket_t _3_edge =
      1ull << _3_bits;  // 262144ms -> 16777216ms(4hour)
  static constexpr bucket_t _2_edge =
      1ull << _2_bits;  // 16777216ms -> 1073741824ms(12day)
  static constexpr bucket_t _1_edge =
      1ull << _1_bits;  // 1073741824ms -> 68719476736ms(795day)
  static constexpr bucket_t _0_edge =
      1ull << _0_bits;  // 68719476736ms -> 4398046511104(50903day)

  // https://stackoverflow.com/questions/76605488/inconsistent-results-when-type-punning-uint64-t-with-union-and-bit-field
  constexpr bucket_t _5() const { return (_time64 >> 0) & (_5_edge - 1); }
  constexpr bucket_t _4() const { return (_time64 >> _5_bits) & (_4_edge - 1); }
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
    return (_time64 >> (_1_bits + _2_bits + _3_bits + _4_bits + _5_bits)) &
           (_0_edge - 1);
  }
};

static constexpr std::size_t bucket_count = clock::_5_edge + clock::_4_edge +
                                            clock::_3_edge + clock::_2_edge +
                                            clock::_1_edge + clock::_0_edge;

struct handle_gen {
 private:
  std::mutex _mutex;
  uint16_t _crc = 0;
  std::queue<timer_handle> _free_ids;

  static timer_handle next() {
    static std::atomic<timer_handle> next_ = 0;
    if (++next_ == invalid_next) {
      next_ = 1;  // warning....
    }
    return next_;
  }

 public:
  handle_gen() = default;
  ~handle_gen() = default;

  static handle_gen &instance() {
    static handle_gen inst;
    return inst;
  }

  static constexpr timer_handle invalid_next = 0xFFFFFFFFull;
  static constexpr timer_handle invalid_handle = 0x7FFFFFFFFFull;

  timer_handle get() noexcept {
    auto make = [](timer_handle handle_, uint16_t &crc) -> timer_handle {
      return (handle_ & invalid_next) | ((++crc & 0x7Full) << 32);
    };

    std::scoped_lock<std::mutex> lock(_mutex);  // todo: 待优化
    if (_free_ids.empty()) {
      static uint16_t default_crc = 1;
      return make(next(), default_crc);
    }
    auto result = _free_ids.front();
    _free_ids.pop();
    return make(result, _crc);
  }

  void put(timer_handle handle_) noexcept {
    std::scoped_lock<std::mutex> lock(_mutex);  // todo: 待优化
    _free_ids.push(handle_ & invalid_next);
  }
};

struct event_interface;
using timer_callback = std::function<void(timer_handle)>;
using timer_stopped_callback =
    std::function<void(std::shared_ptr<event_interface>)>;

struct event_interface {
  timer_handle _handle = handle_gen::invalid_handle;  // 句柄
  time64_t _next = 0;                                 // 下次执行时间
  time64_t _period = 0;                               // 间隔时间
  uint64_t _round = 1;                 // 执行轮次（剩余）
  timer_callback _callback = nullptr;  // 回调
  timer_stopped_callback _stopped_callback = nullptr;  // 停止回调

  std::string _remark{};  // debug remark

  explicit event_interface(time64_t nxt, time64_t period, uint64_t round,
                           timer_callback &&cb,
                           timer_stopped_callback &&stopped_cb)
      : _next(nxt),
        _period(period),
        _round(round),
        _callback(std::forward<timer_callback>(cb)),
        _stopped_callback(std::forward<timer_stopped_callback>(stopped_cb)) {
    _handle = handle_gen::instance().get();
    if (_period == 0)
      _round = 1;
  }
  virtual ~event_interface() {
    if (_handle == handle_gen::invalid_handle)
      return;
    handle_gen::instance().put(_handle);
  }

  virtual time64_t next() = 0;  // next trigger time
};

template <uint64_t precision_tt = 10>
struct event_custom : public event_interface {
  static constexpr time64_t _precision = precision_tt;

  explicit event_custom(time64_t nxt, time64_t period, uint64_t round,
                        timer_callback &&cb,
                        timer_stopped_callback &&stopped_cb)
      : event_interface(nxt, period, round, std::forward<timer_callback>(cb),
                        std::forward<timer_stopped_callback>(stopped_cb)) {}

  ~event_custom() {}

  virtual time64_t next() {
    event_interface::_next = (tick() + _period) / _precision;
    return _next;
  }

  static std::shared_ptr<event_interface> create(
      time64_t nxt, time64_t period, uint64_t round, timer_callback &&cb,
      timer_stopped_callback &&stopped_cb) {
    std::shared_ptr<event_custom> result = std::make_shared<event_custom>(
        nxt, period, round, std::forward<timer_callback>(cb),
        std::forward<timer_stopped_callback>(stopped_cb));
    return result;
  }
};

template <uint64_t precision_tt = 10>
struct event_crontab : public event_interface {
  static constexpr time64_t _precision = precision_tt;

  cron::cronexpr _cronexpr;  // cronexpr

  explicit event_crontab(timer_callback &&cb,
                         timer_stopped_callback &&stopped_cb)
      : event_interface(tick() / _precision, -1, -1,
                        std::forward<timer_callback>(cb),
                        std::forward<timer_stopped_callback>(stopped_cb)) {}

  ~event_crontab() {}

  virtual time64_t next() {
    auto last = event_interface::_next * _precision / 1000;
    event_interface::_next =
        cron::cron_next(_cronexpr, last) * 1000 / _precision;
    return event_interface::_next;
  }

  static std::shared_ptr<event_interface> create(
      const std::string &cron_str, timer_callback &&cb,
      timer_stopped_callback &&stopped_cb) {
    try {
      std::shared_ptr<event_crontab> result = std::make_shared<event_crontab>(
          std::forward<timer_callback>(cb),
          std::forward<timer_stopped_callback>(stopped_cb));
      result->_cronexpr = cron::make_cron(cron_str);
      result->next();
      return result;
    } catch (cron::bad_cronexpr const &ex) {
      // todo: log
      return nullptr;
    }
  }

  static std::shared_ptr<event_interface> create(
      std::string &&cron_str, timer_callback &&cb,
      timer_stopped_callback &&stopped_cb) {
    try {
      std::shared_ptr<event_crontab> result = std::make_shared<event_crontab>(
          std::forward<timer_callback>(cb),
          std::forward<timer_stopped_callback>(stopped_cb));
      result->_cronexpr = cron::make_cron(cron_str);
      result->next();
      return result;
    } catch (cron::bad_cronexpr const &ex) {
      // todo: log
      return nullptr;
    }
  }
};

class alert_interface {
 public:
  alert_interface() = default;
  virtual ~alert_interface() = default;

  virtual void alert_callback(std::shared_ptr<event_interface>) = 0;
  virtual void alert_stopped(std::shared_ptr<event_interface>) = 0;
};

class alert_default final : public alert_interface {
 public:
  alert_default() = default;
  ~alert_default() override = default;

  void alert_callback(std::shared_ptr<event_interface> evt) override {
    if (evt && evt->_callback) {
      evt->_callback(evt->_handle);
    }
  }

  void alert_stopped(std::shared_ptr<event_interface> evt) override {
    if (evt && evt->_stopped_callback) {
      evt->_stopped_callback(evt);
      evt->_stopped_callback = nullptr;
    }
  }
};

template <std::size_t thread_count>
class alert_mt final : public alert_interface {
 private:
  std::array<std::thread, thread_count> _threads;
  std::queue<std::shared_ptr<event_interface>>
      _evt_queue;  // timer_wheel push, _threads pop
  std::mutex _mux;
};

template <uint64_t precision_tt = 10, class mutex_tt = empty_mutex,
          class alert = alert_default>
class timer_wheel {
 private:
  mutex_tt _mutex;
  static constexpr time64_t _precision = precision_tt;  // 精度

  std::vector<std::queue<timer_handle>> _wheels;  // 时间轮
  std::unordered_map<timer_handle, std::shared_ptr<event_interface>> _events;

  time64_t _tick = tick() / _precision;  // 扳手时钟

 public:
  timer_wheel() {
    std::scoped_lock<mutex_tt> lock(_mutex);
    _wheels.resize(bucket_count);
  }

  template <class Rep, class Period>
  inline timer_handle add(const std::chrono::duration<Rep, Period> &when,
                          timer_callback &&callback,
                          timer_stopped_callback &&stopped_callback = nullptr,
                          const time_duration &period = time_duration::zero(),
                          const int64_t round = 0) {
    std::shared_ptr<event_interface> event_ = event_custom<_precision>::create(
        std::chrono::duration_cast<time_duration>(
            (time_clock::now() + when).time_since_epoch())
                .count() /
            _precision,
        period.count(), round, std::forward<timer_callback>(callback),
        std::forward<timer_stopped_callback>(stopped_callback));

    if (event_ == nullptr) {
      return handle_gen::invalid_handle;
    }

    {
      std::scoped_lock<mutex_tt> lock(_mutex);
      _events.emplace(event_->_handle, event_);
      submit_unsafe(event_);
    }
    return event_->_handle;
  }

  inline timer_handle add(const std::string &cron_str,
                          timer_callback &&callback,
                          timer_stopped_callback &&stopped_callback = nullptr) {
    std::shared_ptr<event_interface> event_ = event_crontab<_precision>::create(
        cron_str, std::forward<timer_callback>(callback),
        std::forward<timer_stopped_callback>(stopped_callback));

    {
      std::scoped_lock<mutex_tt> lock(_mutex);
      _events.emplace(event_->_handle, event_);
      submit_unsafe(event_);
    }
    return event_->_handle;
  }

  inline timer_handle add(std::string &&cron_str, timer_callback &&callback,
                          timer_stopped_callback &&stopped_callback = nullptr) {
    std::shared_ptr<event_interface> event_ = event_crontab<_precision>::create(
        std::forward<std::string>(cron_str),
        std::forward<timer_callback>(callback),
        std::forward<timer_stopped_callback>(stopped_callback));

    {
      std::scoped_lock<mutex_tt> lock(_mutex);
      _events.emplace(event_->_handle, event_);
      submit_unsafe(event_);
    }
    return event_->_handle;
  }

  inline time_duration stop(const timer_handle &handle) {
    std::shared_ptr<event_interface> evt = nullptr;
    {
      std::scoped_lock<mutex_tt> lock(_mutex);
      const auto iter = _events.find(handle);
      if (iter == _events.end() || iter->second == nullptr)
        return time_duration(0);
      evt = iter->second;
      _events.erase(iter);
    }

    if (evt->_stopped_callback) {
      evt->_stopped_callback(evt);
      evt->_stopped_callback = nullptr;
    }

    const auto tick_ = tick();
    const auto next_ = evt->_next * _precision;
    if (next_ >= tick_)
      return time_duration(next_ - tick_);
    return time_duration(0);
  }

  inline void execute() {
    const auto tick_now = tick() / _precision;

    while (_tick <= tick_now) {
      clock clk = {_tick};

      if (clk._5()) {
        step_list(_wheels[clk._5()]);
      }
      else if (clk._4()) {
        step_list(_wheels[clk._4() + clock::_5_edge]);
      }
      else if (clk._3()) {
        step_list(_wheels[clk._3() + clock::_4_edge + clock::_5_edge]);
      }
      else if (clk._2()) {
        step_list(_wheels[clk._2() + clock::_3_edge + clock::_4_edge +
                          clock::_5_edge]);
      }
      else if (clk._1()) {
        step_list(_wheels[clk._1() + clock::_2_edge + clock::_3_edge +
                          clock::_4_edge + clock::_5_edge]);
      }
      else if (clk._0()) {
        step_list(_wheels[clk._0() + clock::_1_edge + clock::_2_edge +
                          clock::_3_edge + clock::_4_edge + clock::_5_edge]);
      }

      if (_tick == tick_now)
        break;

      _tick += 1;
    }
  }

 private:
  inline void submit_unsafe(std::shared_ptr<event_interface> evt) {
    if (nullptr == evt)
      return;

    if (evt->_next < _tick) {
      evt->_next = _tick;
    }

    clock clk1 = {evt->_next};
    clock clk2 = {_tick};

    if (clk1._0() != clk2._0()) {
      _wheels[clock::_5_edge + clock::_4_edge + clock::_3_edge +
              clock::_2_edge + clock::_1_edge + clk1._0()]
          .push(evt->_handle);
    }
    else if (clk1._1() != clk2._1()) {
      _wheels[clock::_5_edge + clock::_4_edge + clock::_3_edge +
              clock::_2_edge + clk1._1()]
          .push(evt->_handle);
    }
    else if (clk1._2() != clk2._2()) {
      _wheels[clock::_5_edge + clock::_4_edge + clock::_3_edge + clk1._2()]
          .push(evt->_handle);
    }
    else if (clk1._3() != clk2._3()) {
      _wheels[clock::_5_edge + clock::_4_edge + clk1._3()].push(evt->_handle);
    }
    else if (clk1._4() != clk2._4()) {
      _wheels[clock::_5_edge + clk1._4()].push(evt->_handle);
    }
    else {
      _wheels[clk1._5()].push(evt->_handle);
    }
  }

  inline void step_list(std::queue<timer_handle> &lst) {
    while (true) {
      std::shared_ptr<event_interface> evt = nullptr;
      {
        std::scoped_lock<mutex_tt> lock(_mutex);
        if (lst.empty())
          break;
        auto handle = lst.front();
        lst.pop();

        const auto iter_evt = _events.find(handle);
        if (iter_evt == _events.end())
          continue;

        evt = iter_evt->second;
      }

      if (evt && evt->_next == _tick) {
        if (evt->_round) {
          if (evt->_callback)
            evt->_callback(evt->_handle);
          {
            std::scoped_lock<mutex_tt> lock(_mutex);
            if (_events.find(evt->_handle) == _events.end()) {
              continue;
            }
          }
          evt->_round -= 1;
        }

        if (evt->_round == 0ull) {
          {
            std::scoped_lock<mutex_tt> lock(_mutex);
            _events.erase(evt->_handle);
          }

          if (evt->_stopped_callback)
            evt->_stopped_callback(evt);
          continue;
        }

        evt->next();
      }
      {
        std::scoped_lock<mutex_tt> lock(_mutex);
        submit_unsafe(evt);
      }
    }
  }
};

static timer_wheel<> &instance() {
  static timer_wheel<> inst;
  return inst;
}

};  // end namespace timer

/*
    timer::timer_wheel<10> tw;
    tw.add(std::chrono::milliseconds(1000), [](timer::timer_handle time_h) {
        std::cout << "1s tick....." << time_h << ". " <<
 timer::current_timestamp() << std::endl;
    }, [](std::shared_ptr<timer::event_interface> evt) {
        std::cout << "1 stopped: " << evt->_handle << std::endl;
    }, std::chrono::milliseconds(1000), 10);

    uint32_t count = 0;
    tw.add(std::chrono::milliseconds(50), [&count, &tw](timer::timer_handle
 time_h) { std::cout << "50...tick....." << time_h << ". " <<
 timer::current_timestamp() << std::endl; count += 1; if (count >= 10) {
            tw.stop(time_h);
            tw.add(std::chrono::seconds(1), [](timer::timer_handle time_h) {
                std::cout << "inner 1s...tick....." << time_h << ". " <<
 timer::current_timestamp() << std::endl;
            });
        }
    }, [](std::shared_ptr<timer::event_interface> evt) {
        std::cout << "2 stopped: " << evt->_handle << std::endl;
    }, std::chrono::milliseconds(20), -1);

    while (true) {
        tw.execute();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        //std::cout << "................while....." << std::endl;
    }
 *
 */
