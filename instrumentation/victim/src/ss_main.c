/* The victim entry point. Shared by every part and every library.
 *
 * THE KEY IS COMPILED IN AND THERE IS NO COMMAND TO CHANGE IT. The host
 * needs it only to SCORE a recovery and to content-check the link, and a
 * victim that cannot be rekeyed over the wire is one fewer thing a vendor
 * can object to.
 *
 * It is the FIPS-197 / SP800-38A example key, which is the same key every
 * part measured by the project this grew out of was keyed with, so trace
 * counts are comparable across parts and against that history.
 */
#include <stdint.h>
#include "ss_victim.h"

static const uint8_t KEY[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

int main(void)
{
    ssv_run(KEY);
    return 0;
}
