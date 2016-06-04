#include <iostream>
#include <vector>
#include <string>

/* value semantics */

std::ostream &operator <<(std::ostream &os, const std::tuple<int, int> &x) {
	return os << std::get<0>(x) << ", " << std::get<1>(x);
}

template <typename T>
void draw(const T &x, std::ostream &os, size_t position) {
	os << std::string(position, ' ') << x << std::endl;
}

struct object_t {
	template <typename T>
	object_t(const T &x) : object_(new model<T>(x)) {}
	object_t(const object_t &x) : object_(x.object_->copy_()) {}
	object_t(object_t &&x) = default;
	object_t &operator=(object_t x) {
		object_ = std::move(x.object_);
		return *this;
	}

	friend void draw(const object_t &x, std::ostream &os, size_t position) {
		x.object_->draw_(os, position);
	}

private:
	struct concept_t {
		virtual ~concept_t() = default;
		virtual concept_t *copy_() = 0;
		virtual void draw_(std::ostream &, size_t) const = 0;
	};

	template <typename T>
	struct model : concept_t {
		model(const T &x) : data_(x) {}
		concept_t *copy_() { return new model(*this); }
		void draw_(std::ostream &out, size_t position) const {
			draw(data_, out, position);
		}

		T data_;
	};

	std::unique_ptr<concept_t> object_;
};

using document_t = std::vector<object_t>;

void draw(const document_t &document, std::ostream &os, size_t position) {
	os << std::string(position, ' ') << "<document>" << std::endl;
	for (auto &x : document) {
		draw(x, os, position + 1);
	}
	os << std::string(position, ' ') << "</document>" << std::endl;
}

int main(int argc, char *argv[]) {
	document_t document;
	document.push_back(2);
	document.push_back(std::tuple<int, int>{3, 4});
	draw(document, std::cout, 0);
	return 0;
}
