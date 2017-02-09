#ifndef __TIMERS_H__
#define __TIMERS_H__

/*
  Provides basic hardware timer abstraction for AVRs.

  Usage:
    Timer_Init(0);
    Timer_SetInterval(0,255);
    Timer_SetValue(0,23);
    Timer_SetWave(0,TIMER_WAVE_PWM);
    Timer_SetScale(0,TIMER_SCALE_DIV64);
    sleep(3);
    value = Timer_Value(0);
*/

#define Timer_SetScale(n,scale) Timer_SetScale##n(scale)
#define Timer_SetInterval(n,ticks) Timer_SetInterval##n(ticks)
#define Timer_SetWave(n,wave) Timer_SetWave##n(wave)
#define Timer_Enable(n) Timer_Enable##n
#define Timer_Disable(n) Timer_Disable##n
#define Timer_Value(n) Timer_Value##n
#define Timer_SetValue(n,val) Timer_Value##n = val
#define Timer_Init(n) Timer_Init##n
#define Timer_Interrupts(n) Timer_Interrupts##n
#define Timer_Interrupt_Enable(n,mask) Timer_Interrupts##n |= mask
#define Timer_Interrupt_Disable(n,mask) Timer_Interrupts##n &= ~mask
#define Timer_Interrupt_Flags(n) Timer_Interrupt_Flags##n
// Clearing an IF means writing a 1 to it. Writing a 0 does not change a thing.
#define Timer_Interrupt_Flag_Clear(n,mask) Timer_Interrupt_Flags##n = mask

// For Timer_SetWave:
#define TIMER_WAVE_NORMAL 0

#define TIMER0_WAVE_NORMAL 0
#define TIMER0_WAVE_PWM 1
#define TIMER0_WAVE_CTC 2
#define TIMER0_WAVE_FAST_PWM 3
#define TIMER0_WAVE_PWM_OCRA 5
#define TIMER0_WAVE_FAST_PWM_OCRA 7

#define TIMER1_WAVE_NORMAL 0
#define TIMER1_WAVE_PWM_8BIT 1
#define TIMER1_WAVE_PWM_9BIT 2
#define TIMER1_WAVE_PWM_10BIT 3
#define TIMER1_WAVE_CTC 4
#define TIMER1_WAVE_FAST_PWM_8BIT 5
#define TIMER1_WAVE_FAST_PWM_9BIT 6
#define TIMER1_WAVE_FAST_PWM_10BIT 7
#define TIMER1_WAVE_PWM_FR_ICR 8
#define TIMER1_WAVE_PWM_FR_OCRA 9
#define TIMER1_WAVE_PWM_ICR 10
#define TIMER1_WAVE_PWM_OCRA 11
#define TIMER1_WAVE_CTC_ICR 12
#define TIMER1_WAVE_FAST_PWM_ICR 14
#define TIMER1_WAVE_FAST_PWM_OCRA 15

// For Timer_SetScale:
#define TIMER_SCALE_STOPPED  0
#define TIMER_SCALE_1        1
#define TIMER_SCALE_DIV_8    2
#define TIMER_SCALE_DIV_64   3
#define TIMER_SCALE_DIV_256  4
#define TIMER_SCALE_DIV_1024 5

// For mask in Timer_Interrupt*:
#define TIMER_INTERRUPT_OVERFLOW 1
#define TIMER_INTERRUPT_OUTPUT_COMPARE_A 2
#define TIMER_INTERRUPT_OUTPUT_COMPARE_B 4
#define TIMER_INTERRUPT_OUTPUT_COMPARE_C 8
#define TIMER_INTERRUPT_INPUT_COMPARE 32

#define Timer_SetScale0(scale) \
  TCCR0B = (scale << CS00)
#define Timer_SetInterval0(ticks)
#define Timer_SetWave0(wave) do { \
    TCCR0A  = ((((wave) & 2) >> 1) << WGM01) | (((wave) & 1) << WGM00); \
    TCCR0B  = (TCCR0B & ~(1 << WGM02)) | ((((wave) & 4) >> 2) << WGM02); \
  } while(0)
#define Timer_Enable0 Timer_SetScale0(TIMER_SCALE_1)
#define Timer_Disable0 Timer_SetScale0(TIMER_SCALE_STOPPED)
#define Timer_Value0 TCNT0
#define Timer_Init0 do { \
    TCCR0A = 0; \
    TCCR0B = 0; \
    TIMSK0 = 0; \
    TCNT0  = 0; \
    TIFR0  = 0xff; \
  } while(0)
#define Timer_Interrupts0 TIMSK0
#define Timer_Interrupt_Flags0 TIFR0

#define Timer_SetScale1(scale) \
  TCCR1B = (scale << CS10)
#define Timer_SetInterval1(ticks)
#define Timer_SetWave1(wave) do { \
    TCCR1A  = ((((wave) & 2) >> 1) << WGM11) | (((wave) & 1) << WGM10); \
    TCCR1B  = (TCCR1B & ~(3 << WGM12)) | ((((wave) & 12) >> 2) << WGM12); \
  } while(0)
#define Timer_Enable1 Timer_SetScale1(TIMER_SCALE_1)
#define Timer_Disable1 Timer_SetScale1(TIMER_SCALE_STOPPED)
#define Timer_Value1 TCNT1
#define Timer_Init1 do { \
    TCCR1A = 0; \
    TCCR1B = 0; \
    TIMSK1 = 0; \
    TCNT1  = 0; \
    TIFR1  = 0xff; \
  } while(0)
#define Timer_Interrupts1 TIMSK1
#define Timer_Interrupt_Flags1 TIFR1

// useful with constant parameters --> no flops at runtime, all at compile time
// div is typically one of 1, 8, 64, 256, 1024, corresponding to the
// TIMER_SCALE_DIV_* constant used.
// DO NOT use the TIMER_SCALE_DIV_* constant directly here.
// we do an exxplicit cast to int32_t, to prevent fp arithmetic from dropping
// in when using i += sec2ticks(T,DIV), which would otherwise do so.
// I consider this a bug in gcc.
#define usec2ticks(t,div) ((int32_t)((t)*1e-6*F_CPU/(div)))
#define msec2ticks(t,div) ((int32_t)((t)*1e-3*F_CPU/(div)))
#define sec2ticks(t,div) ((int32_t)((t)*1.0*F_CPU/(div)))

// probably useless, as on chip we will want to calculate everything in ticks:
// DO NOT use the TIMER_SCALE_DIV_* constant directly here.
#define ticks2usec(t,div) ((t)*1e6*(div)/F_CPU)
#define ticks2msec(t,div) ((t)*1e3*(div)/F_CPU)
#define ticks2sec(t,div) ((t)*1.0*(div)/F_CPU)

/*  OCR0A   = 100;
  TCCR0A  = (0 << WGM01)  | (1 << WGM00);
  TCCR0B  = (0 << CS02) | (1 << CS01) | (1 << CS00) | (0 << WGM02);
  TIMSK0  = (1 << OCIE0A) | (1 << TOIE0);
*/

//void Timer_Setup16(uint16 top);
//void Timer_Setup8(uint8 top);

#endif
