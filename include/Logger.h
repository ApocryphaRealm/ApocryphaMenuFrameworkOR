#pragma once

// The framework's logging, as the Skyrim AMF core calls it (logger::info(...) and friends), written onto the spdlog
// default logger that OBSE::Init sets up - the same sink REX::INFO writes to, so one file holds both.

#include <spdlog/spdlog.h>

namespace logger
{
	using level = spdlog::level::level_enum;

	template <class... Args>
	void trace(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->trace(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void debug(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->debug(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void info(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->info(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void warn(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->warn(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void error(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->error(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void critical(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->critical(a_fmt, std::forward<Args>(a_args)...); }

	inline void set_level(level a_log_level, level a_flush_level)
	{
		spdlog::default_logger_raw()->set_level(a_log_level);
		spdlog::default_logger_raw()->flush_on(a_flush_level);
	}

	inline void flush() { spdlog::default_logger_raw()->flush(); }
}
