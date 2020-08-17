#pragma once
#include <memory>
#include <exception>
#include <future>
#include <string>
#include <vector>
#include <map>

// xtransmit
#include "buffer.hpp"
#include "socket.hpp"

// OpenSRT
#include "srt.h"
#include "udt.h"
#include "uriparser.hpp"


namespace xtransmit {
	namespace socket {

		class srt_group
			: public std::enable_shared_from_this<srt_group>
			, public isocket
		{
			using string = std::string;
			using shared_srt_group = std::shared_ptr<srt_group>;

		public:
			explicit srt_group(const std::vector<UriParser>& src_uri);

			srt_group(srt_group& group, int group_id);

			virtual ~srt_group();


		public:
			shared_srt_group	connect();
			shared_srt_group	accept();

			/**
			 * Start listening on the incomming connection requests.
			 *
			 * May throw a socket_exception.
			 */
			void					listen() noexcept(false);

		private:
			void configure(const std::map<string, string>& options);

			void check_options_exist() const;
			int  configure_pre(SRTSOCKET sock);
			int  configure_post(SRTSOCKET sock);
			void create_listeners(const std::vector<UriParser>& src_uri);

		public:
			/**
			 * @returns The number of bytes received.
			 *
			 * @throws socket_exception Thrown on failure.
			 */
			size_t read(const mutable_buffer& buffer, int timeout_ms = -1) final;
			int   write(const const_buffer& buffer, int timeout_ms = -1) final;

			enum connection_mode
			{
				FAILURE = -1,
				LISTENER = 0,
				CALLER = 1,
				RENDEZVOUS = 2
			};

			connection_mode mode() const;

			bool is_caller() const final { return m_mode == CALLER; }

		public:
			int id() const final { return m_bind_socket; }
			int statistics(SRT_TRACEBSTATS& stats, bool instant = true);
			bool supports_statistics() const final { return true; }
			const std::string statistics_csv(bool print_header) final;
			static const std::string stats_to_csv(int socketid, const SRT_TRACEBSTATS& stats, bool print_header);

		private:
			void raise_exception(const string&& place) const;
			void raise_exception(const string&& place, const string&& reason) const;

		private:
			int m_bind_socket = SRT_INVALID_SOCK;
			std::vector<SRTSOCKET> m_listeners;
			int m_epoll_connect = -1;
			int m_epoll_io = -1;

			connection_mode m_mode = FAILURE;
			bool m_blocking_mode = true;
			string m_host;
			int m_port = -1;
			std::map<string, string> m_options; // All other options, as provided in the URI
		};

	} // namespace socket
} // namespace xtransmit

