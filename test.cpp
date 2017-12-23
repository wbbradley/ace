#include <istream>
#include <sstream>
#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
	std::string s = argv[1];
	std::istringstream iss(s);
	int value;
	iss >> value;
	std::cout << value << (iss.eof() ? " OK" : " ERROR") << std::endl;
	return 0;
}

