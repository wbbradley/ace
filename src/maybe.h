#pragma once

template <typename T>
struct maybe_t {
    maybe_t() {}
    maybe_t(T &&t) : pt(new T(std::move(t))) {}
    maybe_t(const T &t) : pt(new T(t)) {}

    maybe_t &operator =(T &&t) const {
	pt.reset(new T(std::move(t)));
	return *this;
    }

    maybe_t &operator =(const T &t) const {
	pt.reset(new T(t));
	return *this;
    }

    const std::unique_ptr<const T> pt;
    bool exists() const { return pt != nullptr; }
    bool empty() const { return pt == nullptr; }
    const T& operator *() const { return *pt; }
};
