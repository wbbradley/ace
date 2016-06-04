#pragma once
#include <istream>

struct rawstreambuf : std::streambuf {
	rawstreambuf(const char *s, off_t n) {
		setg(const_cast<char *>(s),
			 const_cast<char *>(s),
			 const_cast<char *>(s + n));
	}
};

struct hasb {
	hasb(const char *s, off_t n) : rsb(s, n) {
	}
	rawstreambuf rsb;
};

// using base-from-member idiom
struct irawstream : private hasb, std::istream {
	irawstream(const void *s, off_t n)
	: hasb((const char *)s, n), std::istream(&static_cast<hasb *>(this)->rsb) {
		unsetf(ios_base::skipws);
	}
};
