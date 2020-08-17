#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <memory>
#include <thread>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "socket_stats.hpp"
#include "srt_socket.hpp"
#include "srt_socket_group.hpp"
#include "udp_socket.hpp"
#include "receive.hpp"
#include "metrics.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::receive;
using namespace std::chrono;

using shared_srt  = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;

#define LOG_SC_RECEIVE "RECEIVE "




void trace_message(const size_t bytes, const vector<char> &buffer, int conn_id)
{
	::cout << "RECEIVED MESSAGE length " << bytes << " on conn ID " << conn_id;

#if 0
	if (bytes < 50)
	{
		::cout << ":\n";
		::cout << string(buffer.data(), bytes).c_str();
	}
	else if (buffer[0] >= '0' && buffer[0] <= 'z')
	{
		::cout << " (first character):";
		::cout << buffer[0];
	}
#endif
	::cout << endl;

	//CHandShake hs;
	//if (hs.load_from(buffer.data(), buffer.size()) < 0)
	//	return;
	//
	//::cout << "SRT HS: " << hs.show() << endl;
}


void run_pipe(shared_sock src, const config &cfg, const atomic_bool &force_break)
{
	socket::isocket &sock = *src.get();

	vector<char> buffer(cfg.message_size);
	metrics::validator validator;

	auto stat_time = steady_clock::now();

	try
	{
		while (!force_break)
		{
			const size_t bytes = sock.read(mutable_buffer(buffer.data(), buffer.size()), -1);

			if (bytes == 0)
			{
				spdlog::debug(LOG_SC_RECEIVE "sock::read() returned 0 bytes");
				continue;
			}

			if (cfg.print_notifications)
				trace_message(bytes, buffer, sock.id());
			if (cfg.enable_metrics)
				validator.validate_packet(buffer);

			if (cfg.send_reply)
			{
				const string out_message("Message received");
				sock.write(const_buffer(out_message.data(), out_message.size()));

				if (cfg.print_notifications)
					spdlog::error(LOG_SC_RECEIVE "Reply sent on conn ID {}", sock.id());
			}

			const auto tnow = steady_clock::now();
			if (cfg.enable_metrics && tnow > (stat_time + chrono::seconds(1)))
			{
				const auto stats_str = validator.stats();
				spdlog::info(LOG_SC_RECEIVE "{}", stats_str);
				stat_time = tnow;
			}
		}
	}
	catch (const socket::exception &e)
	{
		spdlog::warn(LOG_SC_RECEIVE "{}", e.what());
	}

	if (force_break)
	{
		spdlog::info(LOG_SC_RECEIVE "interrupted by request!");
	}
}

void xtransmit::receive::run(const string &src_url, const config &cfg, const atomic_bool &force_break)
{
	const UriParser uri(src_url);

	shared_sock socket;
	shared_sock connection;

	try
	{
		const bool write_stats = cfg.stats_file != "" && cfg.stats_freq_ms > 0;
		// make_unique is not supported by GCC 4.8, only starting from GCC 4.9 :(
		unique_ptr<socket::stats_writer> stats = write_stats
			? unique_ptr<socket::stats_writer>(new socket::stats_writer(cfg.stats_file, milliseconds(cfg.stats_freq_ms)))
			: nullptr;

		do {
			if (uri.proto() == "udp")
			{
				connection = make_shared<socket::udp>(uri);
			}
			else if (cfg.inputs.empty())
			{
				socket = make_shared<socket::srt>(uri);
				socket::srt* s = static_cast<socket::srt*>(socket.get());
				const bool  accept = s->mode() == socket::srt::LISTENER;
				if (accept)
					s->listen();
				connection = accept ? s->accept() : s->connect();
			}
			else
			{
				vector<UriParser> urls;
				urls.push_back(src_url);
				for (auto u : cfg.inputs)
					urls.push_back(u);

				socket = make_shared<socket::srt_group>(urls);
				socket::srt_group* s = static_cast<socket::srt_group*>(socket.get());
				const bool  accept = s->mode() == socket::srt::LISTENER;
				if (accept)
					s->listen();
				connection = accept ? s->accept() : s->connect();
			}

			if (stats)
				stats->add_socket(connection);
			run_pipe(connection, cfg, force_break);
			if (stats && cfg.reconnect)
				stats->clear();
		} while (cfg.reconnect);
	}
	catch (const socket::exception & e)
	{
		cerr << e.what() << endl;
	}
}

CLI::App* xtransmit::receive::add_subcommand(CLI::App& app, config& cfg, string& src_url)
{
	const map<string, int> to_ms{ {"s", 1000}, {"ms", 1} };

	CLI::App* sc_receive = app.add_subcommand("receive", "Receive data (SRT, UDP)")->fallthrough();
	sc_receive->add_option("src", src_url, "Source URI");
	sc_receive->add_option("--msgsize", cfg.message_size, "Size of a buffer to receive message payload");
	sc_receive->add_option("--statsfile", cfg.stats_file, "output stats report filename");
	sc_receive->add_option("--statsfreq", cfg.stats_freq_ms, "output stats report frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_receive->add_flag("--printmsg", cfg.print_notifications, "print message into to stdout");
	sc_receive->add_flag("--reconnect", cfg.reconnect, "Reconnect automatically");
	sc_receive->add_flag("--enable-metrics", cfg.enable_metrics, "Enable checking metrics: jitter, latency, etc.");
	sc_receive->add_flag("--twoway", cfg.send_reply, "Both send and receive data");
	sc_receive->add_option("--input-group", cfg.inputs, "More input group URLs for SRT bonding");

	return sc_receive;
}



