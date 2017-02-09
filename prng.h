#ifndef __PRNG_H__
#define __PRNG_H__

uint32_t prng_state[16];

uint8_t prng_mangle(uint32_t *state/*[16]*/, uint8_t input) {
  state[0] ^= input ^ (state[1]>>15) ^ (state[5]<<3) ^ state[7] ^ (state[13] << 13) ^ state[15]; // TODO: use something more sane.
  for (int i = 15; i > 0; i--) {
    state[i] = state[i-1];
  }
  return state[15] & 0xff;
}

void prng_seed(uint16_t seed) {
  for (int i = 0; i < 16; i++) {
    prng_state[i] = seed;
  }
  for (int16_t i = 0; i < 4096; i++) {
    uint8_t byte = pgm_read_byte((void*)i);
    prng_mangle(prng_state,byte+seed);
  }
}

uint8_t prng_read_byte() {
  return prng_mangle(prng_state,0);
}

void prng_write_byte(uint8_t byte) {
  prng_mangle(prng_state,byte);
}

#endif
