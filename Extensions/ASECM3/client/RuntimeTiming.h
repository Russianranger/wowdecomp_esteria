#pragma once
#include <chrono>
#include <algorithm>
#include <cstdint>
namespace ASECM3::Client {
struct RuntimeTiming {
    using Clock=std::chrono::steady_clock;
    uint64_t count=0;double total=0,maximum=0;
    void Add(Clock::time_point start){const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();++count;total+=ms;maximum=std::max(maximum,ms);}
    double Average()const{return count?total/count:0;}
};
struct TimeSample {
    RuntimeTiming& value;RuntimeTiming::Clock::time_point start=RuntimeTiming::Clock::now();
    explicit TimeSample(RuntimeTiming& target):value(target){}
    ~TimeSample(){value.Add(start);}
};
}
