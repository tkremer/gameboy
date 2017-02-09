// --- ADC ---

#ifndef __ADC_H__
#define __ADC_H__

#define ADC_REF_EXT 0
#define ADC_REF_VCC 1
#define ADC_REF_1V1 3

#define ADC_SRC_TEMP 8
#define ADC_SRC_1V1 14
#define ADC_SRC_GND 15

// clock divisor is just 2^n with the exception of 0 -> 2,
// but #defines are more readable than log-specified input.
//#define ADC_DIV_2_too 0
#define ADC_DIV_2 1
#define ADC_DIV_4 2
#define ADC_DIV_8 3
#define ADC_DIV_16 4
#define ADC_DIV_32 5
#define ADC_DIV_64 6
#define ADC_DIV_128 7

// enable ADC and disable selected digital pins, CLK_DIV = 2**(prescaler||1)
void adc_conf(bool enable, uint8_t pinmask, uint8_t prescaler) {
  if (enable) {
    PRR &= ~(1<<PRADC); // power the ADC
  }
  DIDR0 = pinmask & 0x3f;
    // disable unused digital IO input buffers.
  ADCSRA = ((enable?1:0) << ADEN) | (1<<ADIF) | ((prescaler & 7) << ADPS0);
    // enable, clear ADIF, set prescaler.
  if (!enable) {
    PRR |= 1<<PRADC; // shut down the ADC
  }
}

// Note, that we assume no ADATE and no concurring messing with the ADC.
// Do not use together with adc_start_continuous.
int16_t adc_read(uint8_t srcpin, uint8_t ref) {
  uint8_t prevmux = ADMUX;
  uint8_t mux = (ref << REFS0) | (srcpin << MUX0); // also ADLAR = 0.
  ADMUX = mux;
  //if (((prevmux >> REFS0) & 3) != ref) {
  if (prevmux != mux) {
    // We may have changed the reference voltage.
    // First read is inaccurate per spec., discard.
    // Same thing holds for a changed source, though that's not in the spec.
    ADCSRA |= 1<<ADSC; // start conversion, also clear ADIF.
    while (!(ADCSRA & (1<<ADIF))); // wait for ADIF.
  }
  ADCSRA |= 1<<ADSC; // start conversion, also clear ADIF.
  // ADCSRB only contains auto-triggering source and ACME. What is ACME?
  while (!(ADCSRA & (1<<ADIF))); // wait for ADIF.
  //while (ADCSRA & ((1<<ADIF) | (1 << ADSC)) == 1<<ADSC);
  int16_t res = ADC;
  ADCSRA = ADCSRA; // clear ADIF.
  return res;
}

void adc_start_continuous(uint8_t srcpin, uint8_t ref) {
  uint8_t mux = (ref << REFS0) | (srcpin << MUX0); // also ADLAR = 0.
  ADMUX = mux;
  ADCSRB = 0; // Free Running mode, ACME=0. Again, WTF is ACME?
  ADCSRA |= (1<<ADATE) | (1<<ADSC); // set to automatic, start and clear ADIF.
}

void adc_stop_continuous() {
  ADCSRA &= ~(1<<ADATE);
  // remove automatic flag. current conversion still continues though.
}

void adc_interrupt_enable(bool enable) {
  if (enable) {
    ADCSRA |= (1<<ADIE);
  } else {
    ADCSRA &= ~(1<<ADIE);
  }
}

bool adc_ready() {
  return ADCSRA & (1<<ADIF);
}

void adc_wait_ready() {
  while (!adc_ready());
}

void adc_clear_ready() {
  ADCSRA = ADCSRA; // clears ADIF
}

int16_t adc_value() {
  return ADC;
}

// takes effect one read after the next one to be started.
void adc_set_channel(uint8_t srcpin, uint8_t ref) {
  uint8_t mux = (ref << REFS0) | (srcpin << MUX0); // also ADLAR = 0.
  ADMUX = mux;
}

#define On_ADC_read ISR (ADC_vect, ISR_BLOCK)

int get_temperature() {
  int16_t res = adc_read(ADC_SRC_TEMP,ADC_REF_1V1);
  int T = 2500 + (res-(int)314*1024.0/1100)*1100.0/1024*130.0/138*100;
//  int T = res;
  return T; // deci-degrees
  // from spec:
  // -45°C +25°C +85°C
  // 242mV  314mV 380mV
  //
  // -> k=(85+45)K/(380-242)mV = 130/138 K/mV
  // (res/1024*1100mV - 314mV) * k + 25°C
  // T = 25 + (res-314*1024/1100)*1100.0/1024*130.0/138;
}

#endif
