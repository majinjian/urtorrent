#include <stdio.h>
#include "bencode.h"

int main(int argc, char *argv[])
{
	int i;

	setbuf(stdout, NULL);

	for (i = 1; i < argc; ++i) {
		be_node *n = be_decode(argv[i]);
		printf("DECODING: %s\n", argv[i]);
		if (n) {
			be_dump(n);
			be_free(n);
		} else
			printf("\tparsing failed!\n");
	}

	return 0;
}
