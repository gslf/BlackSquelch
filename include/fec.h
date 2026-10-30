#ifndef BS_FEC_H
#define BS_FEC_H
#include "protocol.h"
#define BS_FEC_CODED_MAX (((2 * (BS_MAX_BITS + 6) + 47) / 48) * 48)
/* K=7, rate 1/2, generators 171/133 octal, zero initial/final state.
   Positive soft values favor one. Interleaving includes zero padding. */
size_t bs_fec_size(size_t bits);
bool bs_fec_encode(const uint8_t *bits, size_t count, uint8_t *interleaved, size_t capacity);
bool bs_fec_decode(const int8_t *interleaved, size_t count, uint8_t *bits, size_t capacity);
#endif
