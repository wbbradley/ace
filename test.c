#include <stdio.h>
#include <stdlib.h>

uint64_t CityHash64(char *s, size_t len);

int main(int argc, char *argv[]) {
	printf("%lld\n", (long long)CityHash64("funky", 5));
	return 0;
}
