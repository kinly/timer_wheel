# timer_wheel

时间轮定时器

https://kinly.github.io/archives/de38e5db.html


## benchmark test
- via: https://github.com/ki7chen/timer-benchmarks

- 简单测试结果

![image](https://github.com/kinly/timer_wheel/assets/5105129/1b4e4514-f0ee-49d6-a61d-3279e44bc9f3)

## 补充
- 前面反复聊到定时器的问题：
- 复看这个实现，其实不算是一个标准意义的时间轮（参见很多资料，关于时间轮都是只表示最大几天的时间）
- 而之前想要覆盖到一个自己不太（调皮）会活到的时间 (2100年)，把这个时间划分了刻度之后一层层向外分层
  - 可以看到后面几个轮子的刻度都是 1 << 6
  - 算不来了，只是一层层往目标时间戳 (2100年) 补轮子
- 当前（2024.07.28）再看这个定时器实现，暂时还不觉得有什么问题....
- 其他：可以把定时器单独放到一个线程，事件的话最好也分配一个触发线程（=业务线程数）
