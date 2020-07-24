#pragma once
#include <chrono>

namespace xtransmit
{
namespace metrics
{

class latency
{
    typedef std::chrono::system_clock::time_point time_point;
    typedef std::chrono::system_clock::duration duration;

public:
    latency()
    {}

public:
	/// Submit new sample for jitter update.
	/// @param [in] sample_time  the timestamp of the sample
	/// @param [in] current_time current time to compare the timestamp with
    void submit_sample(const time_point& sample_time, const time_point& current_time);

	/// Get curent jitter value.
    int64_t get_latency() const { return m_latency_us; }

private:
    int64_t m_latency_us = 0;
};


} // namespace metrics
} // namespace xtransmit
