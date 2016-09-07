#include "dbg.h"
#include "logger_decls.h"
#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <iomanip>
#include <sstream>
#include <sys/time.h>
#include "disk.h"
#include <execinfo.h>
#include <unistd.h>
#include <csignal>
#include "dbg.h"
#include "utils.h"
#include <cxxabi.h>

int logger_level = log_info | log_warning | log_error | log_panic;

void log_enable(int log_level) {
	logger_level = log_level;
}

logger *_logger = nullptr;

const char *level_color(log_level_t ll) {
	switch (ll)
	{
	case log_info:
		return C_INFO;
	case log_warning:
		return C_WARN;
	case log_error:
		return C_ERROR;
	case log_panic:
		return C_PANIC;
	}
}

const char *logstr(log_level_t ll) {
	switch (ll)
	{
	case log_info:
		return "info: ";
	case log_warning:
		return "warn: ";
	case log_error:
		return "error:";
	case log_panic:
		return "panic:";
	}
}

void write_log_streamv(std::ostream &os, log_level_t level, const location *location, const char *format, va_list args) {
	if (location) {
		os << location->str(true/*vim_mode*/) << ": ";
	}
	os << level_color(level) << logstr(level) << C_RESET << " ";
	os << string_formatv(format, args) << std::endl;
}

void write_log_stream(std::ostream &os, log_level_t level, const location *location, const char *format, ...) {
	va_list args;
	va_start(args, format);
	write_log_streamv(os, level, location, format, args);
	va_end(args);
}

void write_logv(FILE *fp, log_level_t level, const location *location, const char *format, va_list args) {
	std::stringstream ss;
	write_log_streamv(ss, level, location, format, args);

	std::string out = clean_ansi_escapes_if_not_tty(fp, ss.str());

	fprintf(fp, "%s", out.c_str());
	fflush(fp);
}

tee_logger::tee_logger() : logger_old(_logger) {
	_logger = this;
}

tee_logger::~tee_logger() throw() {
	assert(_logger == this);
	_logger = logger_old;
}

void tee_logger::logv(log_level_t level, const location *location, const char *format, va_list args) {
	auto str = string_formatv(format, args);

	captured_logs.push_back({level, location, str});

	if (logger_old != nullptr) {
		logger_old->log(level, location, "%s", str.c_str());
	}
}

void tee_logger::log(log_level_t level, const location *location, const char *format, ...) {
	va_list args;
	va_start(args, format);
	logv(level, location, format, args);
	va_end(args);
}

std::string tee_logger::captured_logs_as_string() const {
	std::stringstream ss;
	for (const auto &tuple : captured_logs) {
		const maybe<location> maybe_location = std::get<1>(tuple);
		write_log_stream(ss, std::get<0>(tuple), maybe_location.as_ptr(),
				"%s", std::get<2>(tuple).c_str());
	}
	return ss.str();
}

indent_logger::indent_logger(int level, std::string msg) :
   	msg(msg), level(level), logger_old(_logger)
{
	debug_above(level, ::log(log_info, c_line_ref("#") " %s", msg.c_str()));
	debug_above(level, ::log(log_info, c_control("(")));
	_logger = this;
}

indent_logger::~indent_logger() throw() {
	_logger = logger_old;
	debug_above(level, ::log(log_info, c_control(")")));
}

void indent_logger::logv(log_level_t level, const location *location, const char *format, va_list args) {
	auto str = string_formatv(format, args);

	if (logger_old != nullptr) {
		logger_old->log(level, location, "%s%s", (location != nullptr) ? "" : "  ", str.c_str());
	}
}

void indent_logger::log(log_level_t level, const location *location, const char *format, ...) {
	va_list args;
	va_start(args, format);
	logv(level, location, format, args);
	va_end(args);
}

standard_logger::standard_logger(const std::string &name, const std::string &root_file_path) : m_name(name), m_fp(NULL) {
	m_root_file_path = root_file_path;
	if (m_root_file_path[m_root_file_path.size() - 1] != '/')
		m_root_file_path.append("/");
	m_root_file_path.append("logs");
	if (!ensure_directory_exists(m_root_file_path))
	{
		fprintf(stderr, "standard_logger : couldn't guarantee that directory %s exists\naborting...\n", m_root_file_path.c_str());
		exit(1);
	}
	if (_logger == NULL)
		_logger = this;
	else
		fprintf(stderr, "multiple loggers are loaded!");

	open();
}

void append_time(std::ostream &os, double time_exact, bool exact, bool for_humans, const char sep) {
	time_t time = (time_t)time_exact;

	tm tdata;
	gmtime_r(&time, &tdata);
	os.setf(std::ios::fixed, std::ios::floatfield);
	os.fill('0'); // Pad on left with '0'
	if (for_humans)
	{
		os << std::setw(2) << tdata.tm_mon + 1 << '/'
			<< std::setw(2) << tdata.tm_mday << '/'
			<< std::setw(2) << tdata.tm_year + 1900 << sep
			<< std::setw(2) << tdata.tm_hour << ':'
			<< std::setw(2) << tdata.tm_min << ':'
			<< std::setw(2) << tdata.tm_sec;
	}
	else
	{
		os << std::setw(2) << tdata.tm_year + 1900
			<< std::setw(2) << tdata.tm_mon + 1
			<< std::setw(2) << tdata.tm_mday << 'T'
			<< std::setw(2) << tdata.tm_hour
			<< std::setw(2) << tdata.tm_min
			<< std::setw(2) << tdata.tm_sec;
		if (exact)
		{
			double decimals = (time_exact - (double)time);
			/* Turn it into milliseconds */
			decimals *= 1000;
			os << '.' << std::setw(3) << (int)decimals;
		}
	}
}

double get_current_time() {
	timeval tv;
	gettimeofday(&tv, NULL);
	double time_now = tv.tv_sec + (double(tv.tv_usec) / 1000000.0);
	return time_now;
}

void time_now(std::ostream &os, bool exact, bool for_humans) {
	double time_exact = get_current_time();
	append_time(os, time_exact, exact, for_humans);
}

void standard_logger::open() {
	std::lock_guard<std::mutex> lock(m_mutex);

	assert(m_fp == NULL);
	if (m_name.size() > 0 && m_root_file_path.size() > 0)
	{
		std::string logfile(m_root_file_path);
		if (logfile[logfile.size() - 1] != '/')
			logfile.append("/");

		std::stringstream ss;
		ss.setf(std::ios::fixed, std::ios::floatfield);
		ss.fill('0'); // Pad on left with '0'
		ss << m_name << "-";
		time_now(ss, false /*exact*/, false /*for_humans*/);
		ss << ".log";
		logfile.append(ss.str());
		m_current_logfile = logfile;
		m_fp = fopen(logfile.c_str(), "wb");
	}
}

void panic_(const char *filename, int line, std::string msg) {
	fprintf(stderr, "%s:%d: PANIC %s\n", filename, line, msg.c_str());
	dbg();
	raise(SIGKILL);
}

void log_location(log_level_t level, const location &location, const char *format, ...) {
	va_list args;
	va_start(args, format);
	logv_location(level, location, format, args);
	va_end(args);
}

void log(log_level_t level, const char *format, ...) {
	if (mask(logger_level,level) == 0)
		return;

	va_list args;
	va_start(args, format);
	logv(level, format, args);
	va_end(args);
}

void logv_location(log_level_t level, const location &location, const char *format, va_list args) {
	if (mask(logger_level,level) == 0)
		return;

	_logger->logv(level, &location, format, args);
}

void logv(log_level_t level, const char *format, va_list args) {
	if (mask(logger_level,level) == 0)
		return;

	_logger->logv(level, nullptr, format, args);
}

void standard_logger::flush() {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_fp != NULL)
		fflush(m_fp);
}

void standard_logger::log(log_level_t level, const location *location, const char *format, ...) {
	if (mask(logger_level, level) == 0)
		return;

	va_list args;
	va_start(args, format);
	logv(level, location, format, args);
	va_end(args);
}

void standard_logger::logv(log_level_t level, const location *location, const char *format, va_list args) {
	if (mask(logger_level, level) == 0) {
		return;
	}

	if (level == log_info) {
		/* if we're not in debugging mode, never emit "info" statements */
		if (debug_level() == 0) {
			return;
		}
	}

	std::lock_guard<std::mutex> lock(m_mutex);

	FILE *fp = m_fp;
	if (fp == NULL)
		fp = stdout;

	write_logv(fp, level, location, format, args);
}

void print_stacktrace(FILE *p_out, unsigned int p_max_frames) {
	fprintf(p_out, "stack trace:\n");
	
	// storage array for stack trace address data
	void **addrlist = (void**)alloca(sizeof(void*) * (p_max_frames + 1));

	// retrieve current stack addresses
	int addrlen = backtrace(addrlist, p_max_frames + 1);
	
	if (addrlen == 0) {
		fprintf(p_out, "  <empty, possibly corrupt>\n");
		return;
	}
	
	// resolve addresses into strings containing "filename(function+address)",
	// this array must be free()-ed
	char** symbollist = backtrace_symbols(addrlist, addrlen);
	
	// allocate string which will be filled with the demangled function name
	size_t funcnamesize = 256;
	char* funcname = (char*)malloc(funcnamesize);
	
	// iterate over the returned symbol lines. skip the first, it is the
	// address of this function.
	for (int i = 2; i < addrlen; i++) {
		char *begin_name = 0, *end_name = 0, *begin_offset = 0, *end_offset = 0;
		
		// find parentheses and +address offset surrounding the mangled name:
		// ./module(function+0x15c) [0x8048a6d]
		for (char *p = symbollist[i]; *p; ++p) {
			if (*p == '(') {
				begin_name = p;
			} else if (*p == '+') {
				begin_offset = p;
			} else if (*p == ')' && begin_offset) {
				end_offset = p;
				break;
			}
		}
		
		// BCH 24 Dec 2014: backtrace_symbols() on OS X seems to return strings in a different, non-standard format.
		// Added this code in an attempt to parse that format.  No doubt it could be done more cleanly.  :->
		if (!(begin_name && begin_offset && end_offset
			  && begin_name < begin_offset)) {
			enum class ParseState {
				kInWhitespace1 = 1,
				kInLineNumber,
				kInWhitespace2,
				kInPackageName,
				kInWhitespace3,
				kInAddress,
				kInWhitespace4,
				kInFunction,
				kInWhitespace5,
				kInPlus,
				kInWhitespace6,
				kInOffset,
				kInOverrun
			};
			ParseState parse_state = ParseState::kInWhitespace1;
			char *p;
			
			for (p = symbollist[i]; *p; ++p) {
				switch (parse_state) {
					case ParseState::kInWhitespace1:	if (!isspace(*p)) parse_state = ParseState::kInLineNumber;	break;
					case ParseState::kInLineNumber:		if (isspace(*p)) parse_state = ParseState::kInWhitespace2;	break;
					case ParseState::kInWhitespace2:	if (!isspace(*p)) parse_state = ParseState::kInPackageName;	break;
					case ParseState::kInPackageName:	if (isspace(*p)) parse_state = ParseState::kInWhitespace3;	break;
					case ParseState::kInWhitespace3:	if (!isspace(*p)) parse_state = ParseState::kInAddress;		break;
					case ParseState::kInAddress:		if (isspace(*p)) parse_state = ParseState::kInWhitespace4;	break;
					case ParseState::kInWhitespace4:
						if (!isspace(*p)) {
							parse_state = ParseState::kInFunction;
							begin_name = p - 1;
						}
						break;
					case ParseState::kInFunction:
						if (isspace(*p)) {
							parse_state = ParseState::kInWhitespace5;
							end_name = p;
						}
						break;
					case ParseState::kInWhitespace5:	if (!isspace(*p)) parse_state = ParseState::kInPlus;		break;
					case ParseState::kInPlus:			if (isspace(*p)) parse_state = ParseState::kInWhitespace6;	break;
					case ParseState::kInWhitespace6:
						if (!isspace(*p)) {
							parse_state = ParseState::kInOffset;
							begin_offset = p - 1;
						}
						break;
					case ParseState::kInOffset:
						if (isspace(*p)) {
							parse_state = ParseState::kInOverrun;
							end_offset = p;
						}
						break;
					case ParseState::kInOverrun:
						break;
				}
			}
			
			if (parse_state == ParseState::kInOffset && !end_offset)
				end_offset = p;
		}
		
		if (begin_name && begin_offset && end_offset
			&& begin_name < begin_offset) {
			*begin_name++ = '\0';
			if (end_name)
				*end_name = '\0';
			*begin_offset++ = '\0';
			*end_offset = '\0';
			
			// mangled name is now in [begin_name, begin_offset) and caller
			// offset in [begin_offset, end_offset). now apply __cxa_demangle():
			
			int status;
			char *ret = abi::__cxa_demangle(begin_name, funcname, &funcnamesize, &status);
			
			if (status == 0) {
				funcname = ret; // use possibly realloc()-ed string; static analyzer doesn't like this but it is OK I think
				fprintf(p_out, "  %s : %s + %s\n", symbollist[i], funcname, begin_offset);
			} else {
				// demangling failed. Output function name as a C function with
				// no arguments.
				fprintf(p_out, "  %s : %s() + %s\n", symbollist[i], begin_name, begin_offset);
			}
		} else {
			// couldn't parse the line? print the whole line.
			fprintf(p_out, "URF:  %s\n", symbollist[i]);
		}
	}
	
	free(funcname);
	free(symbollist);
	
	fflush(p_out);
}

void log_stack(log_level_t level) {
	print_stacktrace(stdout, 100);
	return;

	void *callstack[128];
	int frames = backtrace(callstack, 128);
	char **strs = backtrace_symbols(callstack, frames);
	for (int i = 1; i < frames; ++i) {
		fprintf(stdout, c_line_ref("%s") "\n", strs[i]);
	}
	free(strs);
}

void standard_logger::close() {
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_fp != NULL)
		fclose(m_fp);
	m_fp = NULL;
}

standard_logger::~standard_logger() {
	close();
}

#define CaseError(error) case error: error_string = #error; break

bool check_errno(const char *tag) {
	int err = errno;
	if (err == 0)
		return true;

	const char *error_string = "unknown";
	switch (err)
	{
		CaseError(EACCES);
		CaseError(EAFNOSUPPORT);
		CaseError(EISCONN);
		CaseError(EMFILE);
		CaseError(ENFILE);
		CaseError(ENOBUFS);
		CaseError(ENOMEM);
		CaseError(EPROTO);
		CaseError(EHOSTDOWN);
		CaseError(EHOSTUNREACH);
		CaseError(ENETUNREACH);
		CaseError(EPROTONOSUPPORT);
		CaseError(EPROTOTYPE);
		CaseError(EDQUOT);
		CaseError(EAGAIN);
		CaseError(EBADF);
		CaseError(ECONNRESET);
		CaseError(EFAULT);
		CaseError(EINTR);
		CaseError(EINVAL);
		CaseError(ENETDOWN);
		CaseError(ENOTCONN);
		CaseError(ENOTSOCK);
		CaseError(EOPNOTSUPP);
		CaseError(ETIMEDOUT);
		CaseError(EMSGSIZE);
		CaseError(ECONNREFUSED);
	};
	if (err == -1)
		err = errno;

	log(log_info, "check_errno : %s %s %s", tag, error_string, strerror(err));
	return false;
}

