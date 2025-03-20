#include <stdint.h>
#include <stddef.h>
#include "CH592SFR.h"

#define SLEEPTIME_MS 300

#define SYS_SAFE_ACCESS(a)  do { R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1; \
								 R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2; \
								 asm volatile ("nop\nnop"); \
								 {a} \
								 R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG0; \
								 asm volatile ("nop\nnop"); } while(0)
#define RTC_WAIT_TICKS(t)   uint32_t rtcset = R32_RTC_CNT_32K +(t); while(R32_RTC_CNT_32K <= rtcset)
#define RTC_MAX_COUNT       0xA8C00000
#define RTC_FREQ            32000 // LSI
// #define RTC_FREQ            32768 // LSE
#define CLK_PER_US          (1.0 / ((1.0 / RTC_FREQ) * 1000 * 1000))
#define CLK_PER_MS          (CLK_PER_US * 1000)
#define US_TO_RTC(us)       ((uint32_t)((us) * CLK_PER_US + 0.5))
#define MS_TO_RTC(ms)       ((uint32_t)((ms) * CLK_PER_MS + 0.5))
#define RTC_WAIT_TICKS(t)   uint32_t rtcset = R32_RTC_CNT_32K +(t); while(R32_RTC_CNT_32K <= rtcset)
#define SLEEP_RTC_MIN_TIME  US_TO_RTC(1000)
#define SLEEP_RTC_MAX_TIME  (RTC_MAX_COUNT - 1000 * 1000 * 30)
#define WAKE_UP_RTC_MAX_TIME US_TO_RTC(1600)

#define GPIO_Pin_8             (0x00000100)
#define GPIOA_ResetBits(pin)   (R32_PA_CLR |= (pin))
#define GPIOA_SetBits(pin)     (R32_PA_OUT |= (pin))
#define GPIOA_ModeCfg_Out(pin) R32_PA_PD_DRV &= ~(pin); R32_PA_DIR |= (pin)

#define TXPOWER_MINUS_20_DBM 0x01
#define TXPOWER_MINUS_15_DBM 0x03
#define TXPOWER_MINUS_10_DBM 0x05
#define TXPOWER_MINUS_8_DBM  0x07
#define TXPOWER_MINUS_5_DBM  0x0B
#define TXPOWER_MINUS_3_DBM  0x0F
#define TXPOWER_MINUS_1_DBM  0x13
#define TXPOWER_0_DBM        0x15
#define TXPOWER_1_DBM        0x1B
#define TXPOWER_2_DBM        0x23
#define TXPOWER_3_DBM        0x2B
#define TXPOWER_4_DBM        0x3B

#define __I  volatile const  /*!< defines 'read only' permissions     */
#define __O  volatile        /*!< defines 'write only' permissions     */
#define __IO volatile        /*!< defines 'read / write' permissions   */
/* memory mapped structure for Program Fast Interrupt Controller (PFIC) */
typedef struct
{
	__I uint32_t  ISR[8];           // 0
	__I uint32_t  IPR[8];           // 20H
	__IO uint32_t ITHRESDR;         // 40H
	uint8_t       RESERVED[4];      // 44H
	__O uint32_t  CFGR;             // 48H
	__I uint32_t  GISR;             // 4CH
	__IO uint8_t  VTFIDR[4];        // 50H
	uint8_t       RESERVED0[0x0C];  // 54H
	__IO uint32_t VTFADDR[4];       // 60H
	uint8_t       RESERVED1[0x90];  // 70H
	__O uint32_t  IENR[8];          // 100H
	uint8_t       RESERVED2[0x60];  // 120H
	__O uint32_t  IRER[8];          // 180H
	uint8_t       RESERVED3[0x60];  // 1A0H
	__O uint32_t  IPSR[8];          // 200H
	uint8_t       RESERVED4[0x60];  // 220H
	__O uint32_t  IPRR[8];          // 280H
	uint8_t       RESERVED5[0x60];  // 2A0H
	__IO uint32_t IACTR[8];         // 300H
	uint8_t       RESERVED6[0xE0];  // 320H
	__IO uint8_t  IPRIOR[256];      // 400H
	uint8_t       RESERVED7[0x810]; // 500H
	__IO uint32_t SCTLR;            // D10H
} PFIC_Type;

#define CORE_PERIPH_BASE     (0xE0000000) /* System peripherals base address in the alias region */
#define PFIC_BASE            (CORE_PERIPH_BASE + 0xE000)
#define PFIC                 ((PFIC_Type *) PFIC_BASE)
#define NVIC                 PFIC
#define NVIC_EnableIRQ(IRQn) NVIC->IENR[((uint32_t)(IRQn) >> 5)] = (1 << ((uint32_t)(IRQn) & 0x1F))


extern void BLECoreInit(uint8_t TxPower);
extern void Advertise(uint8_t adv[], size_t len, uint8_t channel);


__attribute__((interrupt))
void RTC_IRQHandler(void) {
	R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);
}

void Clock60MHz() {
	SYS_SAFE_ACCESS(
		R8_PLL_CONFIG &= ~(1 << 5);
		R32_CLK_SYS_CFG = (1 << 6) | (0x48 & 0x1f) | RB_TX_32M_PWR_EN | RB_PLL_PWR_EN; // 60MHz = 0x48
	);
	asm volatile ("nop\nnop\nnop\nnop");
	R8_FLASH_CFG = 0X52;	
	SYS_SAFE_ACCESS(
		R8_PLL_CONFIG |= 1 << 7;
	);
}

void LSIEnable() {
	SYS_SAFE_ACCESS(
		R8_CK32K_CONFIG &= ~(RB_CLK_OSC32K_XT | RB_CLK_XT32K_PON); // turn off LSE
		R8_CK32K_CONFIG |= RB_CLK_INT32K_PON; // turn on LSI
	);
}

void DCDCEnable()
{
	SYS_SAFE_ACCESS(
		R16_AUX_POWER_ADJ |= RB_DCDC_CHARGE;
		R16_POWER_PLAN |= RB_PWR_DCDC_PRE;
	);

	RTC_WAIT_TICKS(2);

	SYS_SAFE_ACCESS(
		R16_POWER_PLAN |= RB_PWR_DCDC_EN;
	);
}

void SleepInit() {
	SYS_SAFE_ACCESS(
		R8_SLP_WAKE_CTRL |= RB_SLP_RTC_WAKE;
		R8_RTC_MODE_CTRL |= RB_RTC_TRIG_EN;
	);
	NVIC_EnableIRQ(RTC_IRQn);
}

void RTCInit() {
	SYS_SAFE_ACCESS(
		R32_RTC_TRIG = 0;
		R8_RTC_MODE_CTRL |= RB_RTC_LOAD_HI;
	);
	while((R32_RTC_TRIG & 0x3FFF) != (R32_RTC_CNT_DAY & 0x3FFF));
	SYS_SAFE_ACCESS(
		R32_RTC_TRIG = 0;
		R8_RTC_MODE_CTRL |= RB_RTC_LOAD_LO;
	);
}

void RTCTrigger(uint32_t cyc) {
	uint32_t t = R32_RTC_CNT_32K + cyc;
	if(t > RTC_MAX_COUNT) {
		t -= RTC_MAX_COUNT;
	}

	SYS_SAFE_ACCESS(
		R32_RTC_TRIG = t;
	);
}

void LowPowerIdle(uint32_t cyc)
{
	RTCTrigger(cyc);

	NVIC->SCTLR &= ~(1 << 2); // sleep
	NVIC->SCTLR &= ~(1 << 3); // wfi
	asm volatile ("wfi\nnop\nnop" );
}

void LowPowerSleep(uint32_t cyc, uint16_t power_plan) {
	RTCTrigger(cyc);

	SYS_SAFE_ACCESS(
		R8_BAT_DET_CTRL = 0;
		R8_XT32K_TUNE = (R16_RTC_CNT_32K > 0x3fff) ? (R8_XT32K_TUNE & 0xfc) | 0x01 : R8_XT32K_TUNE;
		R8_XT32M_TUNE = (R8_XT32M_TUNE & 0xfc) | 0x03;
	);

	NVIC->SCTLR |= (1 << 2); //deep sleep

	SYS_SAFE_ACCESS(
		R8_SLP_POWER_CTRL |= RB_RAM_RET_LV;
		R16_POWER_PLAN = RB_PWR_PLAN_EN | RB_PWR_CORE | power_plan;
		R8_PLL_CONFIG |= (1 << 5);
	);

	NVIC->SCTLR &= ~(1 << 3); // wfi
	asm volatile ("wfi\nnop\nnop" );

	SYS_SAFE_ACCESS(
		R16_POWER_PLAN &= ~RB_XT_PRE_EN;
		R8_PLL_CONFIG &= ~(1 << 5);
		R8_XT32M_TUNE = (R8_XT32M_TUNE & 0xfc) | 0x01;
	);
}

void LowPower(uint32_t time) {
	uint32_t time_sleep, time_curr;
	
	if (time <= WAKE_UP_RTC_MAX_TIME) {
		time = time + (RTC_MAX_COUNT - WAKE_UP_RTC_MAX_TIME);
	}
	else {
		time = time - WAKE_UP_RTC_MAX_TIME;
	}

	time_curr = R32_RTC_CNT_32K;
	if (time < time_curr) {
		time_sleep = time + (RTC_MAX_COUNT - time_curr);
	}
	else {
		time_sleep = time - time_curr;
	}
	
	if ((SLEEP_RTC_MIN_TIME < time_sleep) && (time_sleep < SLEEP_RTC_MAX_TIME)) {
		LowPowerSleep(time, (RB_PWR_RAM2K | RB_PWR_RAM24K | RB_PWR_EXTEND | RB_XT_PRE_EN) );
	}
	else {
		LowPowerIdle(time);
	}
	
	RTCInit();
}

void blink(int n) {
	for(int i = n-1; i >= 0; i--) {
		GPIOA_ResetBits(GPIO_Pin_8);
		LowPowerIdle(MS_TO_RTC(33));
		GPIOA_SetBits(GPIO_Pin_8);
		if(i) LowPowerIdle(MS_TO_RTC(33));
	}
}


int main(void) {
	Clock60MHz();
	DCDCEnable();
	GPIOA_ModeCfg_Out(GPIO_Pin_8);
	GPIOA_SetBits(GPIO_Pin_8);
	LSIEnable();
	RTCInit();
	SleepInit();

	BLECoreInit(TXPOWER_MINUS_3_DBM);
	uint8_t adv[] = {0x66, 0x55, 0x44, 0x33, 0x22, 0xd1, // MAC (reversed)
					0x1e, 0xff, 0x4c, 0x00, 0x12, 0x19, 0x00, // Apple FindMy stuff
					0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xef, 0xfe, 0xdd,0xcc, // key
					0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // more key
					0x00, 0x00}; // status byte and one more
	uint8_t adv_channels[] = {37,38,39};

	blink(5);

	while(1) {
		for(int c = 0; c < sizeof(adv_channels); c++) {
			Advertise(adv, sizeof(adv), adv_channels[c]);
		}
		blink(1);

		LowPower(MS_TO_RTC(SLEEPTIME_MS -33));
		DCDCEnable(); // Sleep disables DCDC
	}
}
