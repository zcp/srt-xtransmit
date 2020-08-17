#include <thread>
#include <iostream>
#include <iterator> // std::ostream_iterator

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "srt_socket_group.hpp"

// srt utils
#include "verbose.hpp"
#include "socketoptions.hpp"
#include "apputil.hpp"

using namespace std;
using namespace xtransmit;
//using shared_srt_group = shared_ptr<socket::srt_group>;


#define LOG_SOCK_SRT "SOCKET::SRT_GROUP "

#if 0

socket::srt_group::srt_group(const vector<UriParser&>& src_uri)
{
	m_bind_socket = srt_create_socket();
	if (m_bind_socket == SRT_INVALID_SOCK)
		throw socket::exception(srt_getlasterror_str());

	if (m_options.count("blocking"))
	{
		m_blocking_mode = !false_names.count(m_options.at("blocking"));
		m_options.erase("blocking");
	}

	if (!m_blocking_mode)
	{
		m_epoll_connect = srt_epoll_create();
		if (m_epoll_connect == -1)
			throw socket::exception(srt_getlasterror_str());

		int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_connect, m_bind_socket, &modes))
			throw socket::exception(srt_getlasterror_str());

		m_epoll_io = srt_epoll_create();
		modes = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, m_bind_socket, &modes))
			throw socket::exception(srt_getlasterror_str());
	}

	check_options_exist();

	if (SRT_SUCCESS != configure_pre(m_bind_socket))
		throw socket::exception(srt_getlasterror_str());

	// Do binding after PRE options are configured in the above call.
	handle_hosts();
}

socket::srt_group::~srt_group()
{
	if (!m_blocking_mode)
	{
		spdlog::debug(LOG_SOCK_SRT "0x{:X} Closing. Releasing epolls", m_bind_socket);
		if (m_epoll_connect != -1)
			srt_epoll_release(m_epoll_connect);
		srt_epoll_release(m_epoll_io);
	}
	spdlog::debug(LOG_SOCK_SRT "0x{:X} Closing", m_bind_socket);
	srt_close(m_bind_socket);
}

void socket::srt_group::create_listeners(const vector<const UriParser&>& src_uri)
{
	// Create listeners according to the parameters
	for (size_t i = 0; i < src_uri.size(); ++i)
	{
		const UriParser& url = src_uri[i];
		sockaddr_any sa = CreateAddr(url.host(), url.portno());

		SRTSOCKET s = srt_create_socket();

		int gcon = 1;
		srt_setsockflag(s, SRTO_GROUPCONNECT, &gcon, sizeof gcon);

		srt_bind(s, sa.get(), sa.size());
		srt_listen(s, 5);

		m_listeners.push_back(s);
	}
}

void socket::srt_group::listen()
{
	int         num_clients = 2;
	int res = srt_listen(m_bind_socket, num_clients);
	if (res == SRT_ERROR)
	{
		srt_close(m_bind_socket);
		raise_exception("listen");
	}

	spdlog::debug(LOG_SOCK_SRT "0x{:X} (srt://{}:{:d}) Listening", m_bind_socket, m_host, m_port);
	res = configure_post(m_bind_socket);
	if (res == SRT_ERROR)
		raise_exception("listen::configure_post");
}

shared_srt_group socket::srt_group::accept()
{
	//spdlog::debug(LOG_SOCK_SRT "0x{:X} (srt://{}:{:d}) {} Waiting for incoming connection",
	//	m_bind_socket, m_host, m_port, m_blocking_mode ? "SYNC" : "ASYNC");

	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{
		raise_exception("non-blocking accept() on a group is not implemented!");
	}

	SRTSOCKET conngrp = srt_accept_bond(m_listeners.data(), m_listeners.size(), -1);
	if (conngrp == SRT_INVALID_SOCK)
	{
		raise_exception("accept_bond failed with {}", srt_getlasterror_str());
	}

	sockaddr_in scl;
	int         sclen = sizeof scl;
	const SRTSOCKET sock = srt_accept(m_bind_socket, (sockaddr*)&scl, &sclen);
	if (sock == SRT_INVALID_SOCK)
	{
		raise_exception("accept");
	}

	// we do one client connection at a time,
	// so close the listener.
	// srt_close(m_bindsock);
	// m_bindsock = SRT_INVALID_SOCK;

	spdlog::debug(LOG_SOCK_SRT "Accepted connection 0x{:X}", sock);

	const int res = configure_post(sock);
	if (res == SRT_ERROR)
		raise_exception("accept::configure_post");

	return make_shared<srt_group>(sock, m_blocking_mode);
}

void socket::srt_group::raise_exception(const string&& place) const
{
	const int    udt_result = srt_getlasterror(nullptr);
	const string message = srt_getlasterror_str();
	spdlog::debug(LOG_SOCK_SRT "0x{:X} {} ERROR {} {}", m_bind_socket, place, udt_result, message);
	throw socket::exception(place + ": " + message);
}

void socket::srt_group::raise_exception(const string&& place, const string&& reason) const
{
	spdlog::debug(LOG_SOCK_SRT "0x{:X} {} ERROR {}", m_bind_socket, place, reason);
	throw socket::exception(place + ": " + reason);
}

shared_srt_group socket::srt_group::connect()
{
	sockaddr_any sa;
	try
	{
		sa = CreateAddr(m_host, m_port);
	}
	catch (const std::invalid_argument & e)
	{
		raise_exception("connect::create_addr", e.what());
	}

	spdlog::debug(LOG_SOCK_SRT "0x{:X} {} Connecting to srt://{}:{:d}",
		m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC", m_host, m_port);

	sockaddr* psa = (sockaddr*)&sa;
	{
		const int res = srt_connect(m_bind_socket, psa, sizeof sa);
		if (res == SRT_ERROR)
		{
			// srt_getrejectreason() added in v1.3.4
			const int reason = srt_getrejectreason(m_bind_socket);
			srt_close(m_bind_socket);
			raise_exception("connect failed", srt_rejectreason_str(reason));
		}
	}

	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{
		// Socket readiness for connection is checked by polling on WRITE allowed sockets.
		int       len = 2;
		SRTSOCKET ready[2];
		if (srt_epoll_wait(m_epoll_connect, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1)
		{
			const SRT_SOCKSTATUS state = srt_getsockstate(m_bind_socket);
			if (state != SRTS_CONNECTED)
			{
				const int reason = srt_getrejectreason(m_bind_socket);
				raise_exception("connect failed", srt_rejectreason_str(reason));
				//raise_exception("connect", "connection failed, socket state " + to_string(state));
			}
		}
		else
		{
			raise_exception("connect.epoll_wait");
		}
	}

	spdlog::debug(LOG_SOCK_SRT "0x{:X} {} Connected to srt://{}:{:d}",
		m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC", m_host, m_port);
	{
		const int res = configure_post(m_bind_socket);
		if (res == SRT_ERROR)
			raise_exception("connect::onfigure_post");
	}

	return shared_from_this();
}

void socket::srt_group::check_options_exist() const
{
#ifdef ENABLE_CXX17
	for (const auto& [key, val] : myMap)
	{
#else
	for (const auto el : m_options)
	{
		const string& key = el.first;
		const string& val = el.second;
#endif
		bool opt_found = false;
		for (const auto o : srt_options)
		{
			if (o.name != key)
				continue;

			opt_found = true;
			break;
		}

		if (opt_found || key == "bind")
			continue;

		spdlog::warn(LOG_SOCK_SRT "srt://{}:{:d}: Ignoring socket option '{}={}' (not recognized)!",
			m_host, m_port, key, val);
	}
	}


int socket::srt_group::configure_pre(SRTSOCKET sock)
{
	int maybe = m_blocking_mode ? 1 : 0;
	int result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &maybe, sizeof maybe);
	if (result == -1)
		return result;

	// host is only checked for emptiness and depending on that the connection mode is selected.
	// Here we are not exactly interested with that information.
	std::vector<string> failures;

	// NOTE: here host = "", so the 'connmode' will be returned as LISTENER always,
	// but it doesn't matter here. We don't use 'connmode' for anything else than
	// checking for failures.
	SocketOption::Mode conmode = SrtConfigurePre(sock, m_host, m_options, &failures);

	if (conmode == SocketOption::FAILURE)
	{
		if (Verbose::on)
		{
			Verb() << "WARNING: failed to set options: ";
			copy(failures.begin(), failures.end(), ostream_iterator<string>(*Verbose::cverb, ", "));
			Verb();
		}

		return SRT_ERROR;
	}

	m_mode = static_cast<connection_mode>(conmode);

	return SRT_SUCCESS;
}

int socket::srt_group::configure_post(SRTSOCKET sock)
{
	int is_blocking = m_blocking_mode ? 1 : 0;

	int result = srt_setsockopt(sock, 0, SRTO_SNDSYN, &is_blocking, sizeof is_blocking);
	if (result == -1)
		return result;
	result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &is_blocking, sizeof is_blocking);
	if (result == -1)
		return result;

	// host is only checked for emptiness and depending on that the connection mode is selected.
	// Here we are not exactly interested with that information.
	vector<string> failures;

	SrtConfigurePost(sock, m_options, &failures);

	if (!failures.empty())
	{
		if (Verbose::on)
		{
			Verb() << "WARNING: failed to set options: ";
			copy(failures.begin(), failures.end(), ostream_iterator<string>(*Verbose::cverb, ", "));
			Verb();
		}
	}

	return 0;
}

void socket::srt_group::handle_hosts()
{
	const auto bind_me = [&](const sockaddr* sa) {
		const int       bind_res = srt_bind(m_bind_socket, sa, sizeof * sa);
		if (bind_res < 0)
		{
			srt_close(m_bind_socket);
			throw socket::exception("SRT binding has failed");
		}
	};

	bool ip_bonded = false;
	if (m_options.count("bind"))
	{
		string bindipport = m_options.at("bind");
		transform(bindipport.begin(), bindipport.end(), bindipport.begin(), [](char c) { return tolower(c); });
		const size_t idx = bindipport.find(":");
		const string bindip = bindipport.substr(0, idx);
		const int bindport = idx != string::npos
			? stoi(bindipport.substr(idx + 1, bindipport.size() - (idx + 1)))
			: m_port;
		m_options.erase("bind");

		sockaddr_any sa_bind;
		try
		{
			sa_bind = CreateAddr(bindip, bindport);
		}
		catch (const std::invalid_argument&)
		{
			throw socket::exception("create_addr_inet failed");
		}

		bind_me(reinterpret_cast<const sockaddr*>(&sa_bind));
		ip_bonded = true;

		spdlog::info(LOG_SOCK_SRT "srt://{}:{:d}: bound to '{}:{}'.",
			m_host, m_port, bindip, bindport);
	}

	if (m_host == "" && !ip_bonded)
	{
		// bind listener
		sockaddr_any sa;
		try
		{
			sa = CreateAddr(m_host, m_port);
		}
		catch (const std::invalid_argument & e)
		{
			raise_exception("listen::create_addr", e.what());
		}
		bind_me(reinterpret_cast<const sockaddr*>(&sa));
	}
}

size_t socket::srt_group::read(const mutable_buffer & buffer, int timeout_ms)
{
	if (!m_blocking_mode)
	{
		int ready[2] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
		int len = 2;

		const int epoll_res = srt_epoll_wait(m_epoll_io, ready, &len, nullptr, nullptr, timeout_ms, 0, 0, 0, 0);
		if (epoll_res == SRT_ERROR)
		{
			if (srt_getlasterror(nullptr) == SRT_ETIMEOUT)
				return 0;

			raise_exception("read::epoll");
		}
	}

	const int res = srt_recvmsg2(m_bind_socket, static_cast<char*>(buffer.data()), (int)buffer.size(), nullptr);
	if (SRT_ERROR == res)
	{
		if (srt_getlasterror(nullptr) != SRT_EASYNCRCV)
			raise_exception("read::recv");

		spdlog::warn(LOG_SOCK_SRT "recvmsg returned error 6002: read error, try again");
		return 0;
	}

	return static_cast<size_t>(res);
}

int socket::srt_group::write(const const_buffer & buffer, int timeout_ms)
{
	stringstream ss;
	if (!m_blocking_mode)
	{
		int ready[2] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
		int len = 2;
		int rready[2] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
		int rlen = 2;
		// TODO: check error fds
		const int res = srt_epoll_wait(m_epoll_io, rready, &rlen, ready, &len, timeout_ms, 0, 0, 0, 0);
		if (res == SRT_ERROR)
			raise_exception("write::epoll");

		ss << "write::epoll_wait result " << res << " rlen " << rlen << " wlen " << len << " wsocket " << ready[0];
		//Verb() << "srt::socket::write: srt_epoll_wait set len " << len << " socket " << ready[0];
	}

	const int res = srt_sendmsg2(m_bind_socket, static_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), nullptr);
	if (res == SRT_ERROR)
	{
		if (srt_getlasterror(nullptr) != SRT_EASYNCSND)
			return 0;

		size_t blocks, bytes;
		srt_getsndbuffer(m_bind_socket, &blocks, &bytes);
		int sndbuf = 0;
		int optlen = sizeof sndbuf;
		srt_getsockopt(m_bind_socket, 0, SRTO_SNDBUF, &sndbuf, &optlen);
		ss << " SND Buffer: " << bytes << " / " << sndbuf << " bytes";
		ss << " (" << sndbuf - bytes << " bytes remaining)";
		ss << "trying to write " << buffer.size() << "bytes";
		raise_exception("socket::write::send", srt_getlasterror_str() + ss.str());
	}

	return res;
}

socket::srt_group::connection_mode socket::srt::mode() const
{
	return m_mode;
}

int socket::srt_group::statistics(SRT_TRACEBSTATS & stats, bool instant)
{
	return srt_bstats(m_bind_socket, &stats, instant);
}

const string socket::srt_group::stats_to_csv(int socketid, const SRT_TRACEBSTATS & stats, bool print_header)
{
	std::ostringstream output;

	// Note: std::put_time is supported only in GCC 5 and higher
#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ >= 5)
#define HAS_PUT_TIME
#endif

#define HAS_PKT_REORDER_TOL (SRT_VERSION_MAJOR >= 1) && (SRT_VERSION_MINOR >= 4) && (SRT_VERSION_PATCH > 0)

	if (print_header)
	{
#ifdef HAS_PUT_TIME
		output << "Timepoint,";
#endif
		output << "Time,SocketID,pktFlowWindow,pktCongestionWindow,pktFlightSize,";
		output << "msRTT,mbpsBandwidth,mbpsMaxBW,pktSent,pktSndLoss,pktSndDrop,";
		output << "pktRetrans,byteSent,byteAvailSndBuf,byteSndDrop,mbpsSendRate,usPktSndPeriod,msSndBuf,";
		output << "pktRecv,pktRcvLoss,pktRcvDrop,pktRcvRetrans,pktRcvBelated,";
		output << "byteRecv,byteAvailRcvBuf,byteRcvLoss,byteRcvDrop,mbpsRecvRate,msRcvBuf,msRcvTsbPdDelay";
#if HAS_PKT_REORDER_TOL
		output << ",msRcvTsbPdDelay";
#endif
		output << endl;
		return output.str();
	}

#ifdef HAS_PUT_TIME
	auto print_timestamp = [&output]() {
		using namespace std;
		using namespace std::chrono;

		const auto systime_now = system_clock::now();
		const time_t time_now = system_clock::to_time_t(systime_now);
		std::tm* tm_now = std::localtime(&time_now);
		if (!tm_now)
		{
			spdlog::warn(LOG_SOCK_SRT "Failed to get current time for stats");
			return;
		}

		output << std::put_time(tm_now, "%d.%m.%Y %T.") << std::setfill('0') << std::setw(6);
		const auto since_epoch = systime_now.time_since_epoch();
		const seconds s = duration_cast<seconds>(since_epoch);
		output << duration_cast<microseconds>(since_epoch - s).count();
		output << std::put_time(tm_now, " %z");
		output << ",";
	};

	print_timestamp();
#endif // HAS_PUT_TIME

	output << stats.msTimeStamp << ",";
	output << socketid << ",";
	output << stats.pktFlowWindow << ",";
	output << stats.pktCongestionWindow << ",";
	output << stats.pktFlightSize << ",";

	output << stats.msRTT << ",";
	output << stats.mbpsBandwidth << ",";
	output << stats.mbpsMaxBW << ",";
	output << stats.pktSent << ",";
	output << stats.pktSndLoss << ",";
	output << stats.pktSndDrop << ",";

	output << stats.pktRetrans << ",";
	output << stats.byteSent << ",";
	output << stats.byteAvailSndBuf << ",";
	output << stats.byteSndDrop << ",";
	output << stats.mbpsSendRate << ",";
	output << stats.usPktSndPeriod << ",";
	output << stats.msSndBuf << ",";

	output << stats.pktRecv << ",";
	output << stats.pktRcvLoss << ",";
	output << stats.pktRcvDrop << ",";
	output << stats.pktRcvRetrans << ",";
	output << stats.pktRcvBelated << ",";

	output << stats.byteRecv << ",";
	output << stats.byteAvailRcvBuf << ",";
	output << stats.byteRcvLoss << ",";
	output << stats.byteRcvDrop << ",";
	output << stats.mbpsRecvRate << ",";
	output << stats.msRcvBuf << ",";
	output << stats.msRcvTsbPdDelay;

#if	HAS_PKT_REORDER_TOL
	output << "," << stats.pktReorderTolerance;
#endif

	output << endl;

	return output.str();

#undef HAS_PUT_TIME
}

const string socket::srt_group::statistics_csv(bool print_header)
{
	SRT_TRACEBSTATS stats;
	if (SRT_ERROR == srt_bstats(m_bind_socket, &stats, true))
		raise_exception("statistics");

	return stats_to_csv(m_bind_socket, stats, print_header);
}

#endif