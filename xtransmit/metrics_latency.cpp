#include <algorithm>
#include <sstream>
#include <iostream>
#include <iomanip> // put_time
#include "metrics_latency.hpp"


namespace xtransmit {
namespace metrics {

using namespace std;
using namespace chrono;

void latency::submit_sample(const time_point& sample_time, const time_point& current_time)
{
	auto in_time_t = std::chrono::system_clock::to_time_t(sample_time);
	// std::stringstream ss;
    // ss << "Sample: " << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
	// in_time_t = std::chrono::system_clock::to_time_t(current_time);
	// ss << " now: " << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");

	const auto delay = current_time - sample_time;
	// ss << " diff: " << duration_cast<microseconds>(delay).count();
	// std::cerr << ss.str() << endl;
	m_latency_us = max(m_latency_us, duration_cast<microseconds>(delay).count());
}


} // namespace metrics
} // namespace xtransmit