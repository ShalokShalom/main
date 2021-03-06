/*
 * Convert Intel microcode.dat into individual ucode files
 * named: intel-ucode/$family-$model-$stepping
 *
 * The subdir intel-ucode/ is created in the current working
 * directory. We get multiple ucodes in the same file, so they
 * are appended to an existing file. Make sure the directory
 * is empty before every run of the converter.
 *
 * Kay Sievers <kay.sievers@vrfy.org>
 * Anton Arapov <anton@redhat.com>
 */


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <stdbool.h>
#include <inttypes.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

struct microcode_header_intel {
	unsigned int hdrver;
	unsigned int rev;
	unsigned int date;
	unsigned int sig;
	unsigned int cksum;
	unsigned int ldrver;
	unsigned int pf;
	unsigned int datasize;
	unsigned int totalsize;
	unsigned int reserved[3];
};

union mcbuf {
	struct microcode_header_intel hdr;
	unsigned int i[0];
	char c[0];
};

int main(int argc, char *argv[])
{
	char *filename = "/lib/firmware/microcode.dat";
	FILE *input, *f;
	char line[LINE_MAX];
	char buf[4000000];
	union mcbuf *mc;
	size_t bufsize, count, start;
	int rc = EXIT_SUCCESS;

	if (argv[1] != NULL)
		filename = argv[1];

	if (!strcmp(filename, "-")) {
		input = stdin;
	} else {
		input = fopen(filename, "re");
		if (input == NULL) {
			printf("open %s: %m\n", filename);
			rc = EXIT_FAILURE;
			goto out;
		}
	}

	count = 0;
	mc = (union mcbuf *) buf;
	while (fgets(line, sizeof(line), input) != NULL) {
		if (sscanf(line, "%x, %x, %x, %x",
		    &mc->i[count],
		    &mc->i[count + 1],
		    &mc->i[count + 2],
		    &mc->i[count + 3]) != 4)
			continue;
		count += 4;
	}
	fclose(input);

	bufsize = count * sizeof(int);
	printf("%s: %lu(%luk) bytes, %zu integers\n",
	       filename,
	       bufsize,
	       bufsize / 1024,
	       count);

	if (bufsize < sizeof(struct microcode_header_intel))
		goto out;

	mkdir("intel-ucode", 0750);

	start = 0;
	for (;;) {
		size_t size;
		unsigned int family, model, stepping;
		unsigned int year, month, day;

		mc = (union mcbuf *) &buf[start];

		if (mc->hdr.totalsize)
			size = mc->hdr.totalsize;
		else
			size = 2000 + sizeof(struct microcode_header_intel);

		if (mc->hdr.ldrver != 1 || mc->hdr.hdrver != 1) {
			printf("unknown version/format:\n");
			rc = EXIT_FAILURE;
			break;
		}

		/*
		 *  0- 3 stepping
		 *  4- 7 model
		 *  8-11 family
		 * 12-13 type
		 * 16-19 extended model
		 * 20-27 extended family
		 */
		family = (mc->hdr.sig >> 8) & 0xf;
		if (family == 0xf)
			family += (mc->hdr.sig >> 20) & 0xff;
		model = (mc->hdr.sig >> 4) & 0x0f;
		if (family == 0x06)
			model += ((mc->hdr.sig >> 16) & 0x0f) << 4;
		stepping = mc->hdr.sig & 0x0f;

		year = mc->hdr.date & 0xffff;
		month = mc->hdr.date >> 24;
		day = (mc->hdr.date >> 16) & 0xff;

		asprintf(&filename, "intel-ucode/%02x-%02x-%02x", family, model, stepping);
		printf("\n");
		printf("%s\n", filename);
		printf("signature: 0x%02x\n", mc->hdr.sig);
		printf("flags:     0x%02x\n", mc->hdr.pf);
		printf("revision:  0x%02x\n", mc->hdr.rev);
		printf("date:      %04x-%02x-%02x\n", year, month, day);
		printf("size:      %zu\n", size);

		f = fopen(filename, "ae");
		if (f == NULL) {
			printf("open %s: %m\n", filename);
			rc = EXIT_FAILURE;
			goto out;
		}
		if (fwrite(mc, size, 1, f) != 1) {
			printf("write %s: %m\n", filename);
			rc = EXIT_FAILURE;
			goto out;
		}
		fclose(f);
		free(filename);

		start += size;
		if (start >= bufsize)
			break;
	}
	printf("\n");

 out:
	return rc;
}
