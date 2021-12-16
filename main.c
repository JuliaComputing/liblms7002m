#include "liblitepcie.h"
#include "liblms7002m.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>

#define LITEPCIE_SPI_CS_HIGH (0 << 0)
#define LITEPCIE_SPI_CS_LOW  (1 << 0)
#define LITEPCIE_SPI_START   (1 << 0)
#define LITEPCIE_SPI_DONE    (1 << 0)
#define LITEPCIE_SPI_LENGTH  (1 << 8)

static int fd;

const char *bitstring(int x, int N_bits){
    static char b[512];
    char *p = b;
    b[0] = '\0';

    for(int i=(N_bits-1); i>=0; i--){
      if (i < N_bits-1 && !((i+1)%8))
        *p++ = ' ';
      *p++ = (x & (1<<i)) ? '1' : '0';
    }
    return b;
}

void lms7_log_ex(struct lms7_state* s,
				 const char* function,
				 const char* file,
				 int line_no,
				 const char* fmt, ...)
{
	char logbuffer[1024];
	int len;
	va_list ap;
	va_start(ap, fmt);

	len = vsnprintf(logbuffer, sizeof(logbuffer) - 1, fmt, ap);
	if (len > sizeof(logbuffer) - 1)
		logbuffer[sizeof(logbuffer) - 1] = 0;
	else if (len < 0)
		logbuffer[0] = 0;
	va_end(ap);

	printf("%s at %s:%d: %s\n", function, file, line_no, logbuffer);
}

uint32_t xtrxll_lms7_spi(const uint32_t data_in)
{
    //load tx data
    uint16_t addr = (data_in >> 16) & ((1<<15)-1);
    uint16_t val = data_in & ((1<<16)-1);
    bool is_write = data_in & (1<<31);
    if (is_write) {
        printf("SPI request: 0x%08x (write 0x%04x, or %s, at 0x%04x)\n", data_in, val, bitstring(val, 16), addr);
    } else {
        printf("SPI request: 0x%08x (read from 0x%04x)\n", data_in, addr);
    }
    litepcie_writel(fd, CSR_LMS7002M_SPI_MOSI_ADDR, data_in);

    //start transaction
    litepcie_writel(fd, CSR_LMS7002M_SPI_CONTROL_ADDR, 32*LITEPCIE_SPI_LENGTH | LITEPCIE_SPI_START);

    //wait for completion
    while ((litepcie_readl(fd, CSR_LMS7002M_SPI_STATUS_ADDR) & LITEPCIE_SPI_DONE) == 0);

    //load rx data
    if (!is_write) {
        uint32_t data_out = litepcie_readl(fd, CSR_LMS7002M_SPI_MISO_ADDR) & 0xffff;
        val = data_out & ((1<<16)-1);
        printf("SPI reply: 0x%08x (read 0x%04x, or %s)\n", data_out, val, bitstring(val, 16));
        return data_out;
    }
}

int xtrxll_lms7_spi_bulk(const uint32_t* out, uint32_t* in, size_t count) {
    for (int i = 0; i < count; i++)
        in[i] = xtrxll_lms7_spi(out[i]);
    return 0;
}

int lms7_spi_transact(struct lms7_state* s, uint16_t ival, uint32_t* oval)
{
	uint32_t v = (uint32_t)ival<<16;
	return xtrxll_lms7_spi_bulk(&v, oval, 1);
}

int lms7_spi_post(struct lms7_state* s, const unsigned count, const uint32_t* regs)
{
	uint32_t dummy[count];
	return xtrxll_lms7_spi_bulk(regs, dummy, count);
}


int main(int argc, char **argv) {

    if (argc < 2)
    {
        printf("Usage %s /dev/litepcieX\n", argv[0]);
        return EXIT_FAILURE;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    printf("Read scratch 0x%x\n", litepcie_readl(fd, CSR_CTRL_SCRATCH_ADDR));

    struct lms7_state lms_state;
	int res = lms7_enable(&lms_state);
	if (res) {
        printf("lms7_enable failed\n");
		return EXIT_FAILURE;
	}

	// 1. Set CGEN frequency
	// --------------------------------------------------------------
    // XXX: dummy values; need to reverse the actual logic from
    //      https://github.com/xtrx-sdr/libxtrx/blob/master/xtrx_fe_nlms7.c
    int dacdiv = 1;
    double cgen_rate = 801e6;

	for (unsigned j = 0; j < 40; j++) {
		unsigned clkdiv = (dacdiv == 1) ? 0 :
						  (dacdiv == 2) ? 1 :
						  (dacdiv == 4) ? 2 : 3;

        // NOTE: this crashes, but after running it once the other driver
        //       suddenly is able to tune (although lms7_cgen_tune_sync here
        //       might not matter for that, and lms7_enable might be sufficient)
		res = lms7_cgen_tune_sync(&lms_state,
								  cgen_rate,
								  clkdiv);
		if (res == 0) {
			// TODO FIXME!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!11
			break;
		}
	}
	if (res != 0) {
		printf("can't tune VCO for data clock\n");
        return EXIT_SUCCESS;
	}

    close(fd);

    printf("Done!\n");
    return EXIT_SUCCESS;
}
