#include "HAL.h"
#define TXPOWER_MINUS_20_DBM       0x01
#define TXPOWER_MINUS_15_DBM       0x03
#define TXPOWER_MINUS_10_DBM       0x05
#define TXPOWER_MINUS_8_DBM        0x07
#define TXPOWER_MINUS_5_DBM        0x0B
#define TXPOWER_MINUS_3_DBM        0x0F
#define TXPOWER_MINUS_1_DBM        0x13
#define TXPOWER_0_DBM              0x15
#define TXPOWER_1_DBM              0x1B
#define TXPOWER_2_DBM              0x23
#define TXPOWER_3_DBM              0x2B
#define TXPOWER_4_DBM              0x3B

extern uint8_t dtmFlag;
extern uint32_t volatile **gPaControl;
extern uint32_t *gptrAESReg;
extern uint32_t *gptrLLEReg;
extern uint32_t volatile *gptrRFENDReg; // needs volatile, otherwise part of the tuning process is optimized out
extern uint32_t *gptrBBReg;

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];
uint32_t g_LLE_IRQLibHandlerLocation;
volatile uint8_t tx_end_flag = 0;
extern rfConfig_t rfConfig;
extern bleConfig_t ble;
extern uint8_t tmosSign;

extern uint32_t (*fnGetClockCBs)();

struct bleClock {
	uint32_t par0;
	uint32_t par1;
	uint16_t par2;
	uint16_t par3;
	uint8_t par4;
	uint8_t par5;
	uint8_t par6;
	uint8_t par7;
};
extern struct bleClock bleClock_t; // this name sucks

struct bleIPPara_t {
	uint8_t par0;
	uint8_t par1;
	uint8_t par2;
	uint8_t par3;
	uint8_t par4;
	uint8_t par5;
	uint8_t par6;
	uint8_t par7;
	uint8_t par8;
	uint8_t par9;
	uint8_t par10;
	uint8_t par11;
	uint8_t par12;
	uint8_t par13;
	uint32_t par14;
	uint32_t par15;
	uint32_t par16;
};
extern struct bleIPPara_t gBleIPPara;

struct rfInfo_t {
	uint8_t par0;
	uint8_t par1;
	uint8_t par2;
	uint8_t par3;
	uint32_t par4;
	int32_t par5;
	uint8_t par6;
	uint8_t par7;
	uint8_t par8;
	uint8_t par9;
	uint8_t par10;
	uint8_t par11;
	/* there might be more stuff */
};
extern struct rfInfo_t rfInfo;

__HIGH_CODE
__attribute__((noinline))
void RF_Wait_Tx_End() {
	tx_end_flag = FALSE;
	uint32_t i = 0;
	while(!tx_end_flag) {
		i++;
		__nop();
		__nop();
		if(i > (FREQ_SYS/1000)) {
			tx_end_flag = TRUE;
		}
	}
}

void Lib_Calibration_LSI(void) {
	Calibration_LSI(Level_64);
}

void DevInit(uint8_t TxPower) {
	gptrLLEReg[3] = 0x1f000f;
	gptrLLEReg[5] = 0x8c;
	gptrLLEReg[6] = 0x78;
	gptrLLEReg[7] = 0x76;
	gptrLLEReg[8] = 0xffffffff;
	gptrLLEReg[9] = 0x8c;
	gptrLLEReg[11] = 0x6e;
	gptrLLEReg[13] = 0x8c;
	gptrLLEReg[15] = 0x6e;
	gptrLLEReg[17] = 0x8c;
	gptrLLEReg[19] = 0x76;
	gptrLLEReg[21] = 0x14;
	gptrLLEReg[31] = (uint32_t)MEM_BUF;

	gptrRFENDReg[10] = 0x480;
	gptrRFENDReg[12] = gptrRFENDReg[12] & 0x8fffffff | 0x10077700;
	gptrRFENDReg[15] = gptrRFENDReg[15] & 0x18ff0fff | 0x42005000;
	gptrRFENDReg[19] &= 0xfffffff8;
	gptrRFENDReg[21] = gptrRFENDReg[21] & 0xfffffff0 | 9;
	gptrRFENDReg[23] &= 0xff88ffff;

	gptrBBReg[0] |= 0x800000;
	gptrBBReg[13] = 0x50;

	gptrBBReg[11] |= 0x80000000;
	gptrBBReg[11] = ((TxPower & 0x3f) << 0x19) | (gptrBBReg[11] & 0x81ffffff);
	uint32_t uVar3 = 0x1000000;
	uint32_t uVar4 = gptrRFENDReg[23] & 0xf8ffffff;
	if(TxPower < 29) {
		/* uVar3 and uVar4 are initialized properly already */
	}
	else if(TxPower < 35) {
		uVar3 = 0x3000000;
	}
	else if(TxPower < 59) {
		uVar3 = 0x5000000;
	}
	else {
		uVar4 = gptrRFENDReg[23];
		uVar3 = 0x7000000;
	}
	gptrRFENDReg[23] = uVar4 | uVar3;
	gptrBBReg[4] = gptrBBReg[4] & 0xffffffc0 | 0xe;
	// DAT_e000e053 = 0x14; // radio.h: PFIC->IDCFGR[3] = 0x14;
	// _DAT_e000e06c = 0x200016cf; // radio.h: PFIC->FIADDRR[3] = (uint32_t)(&BB_IRQLibFunction) | 1;
}

void RFEND_TxTuneWait() {
	gptrLLEReg[25] = 8000;
	while((-1 < (int32_t)gptrRFENDReg[36] << 5) || (-1 < (int32_t)gptrRFENDReg[36] << 6)) {
		if(gptrLLEReg[25] == 0) {
			break;
		}
	}
}

void RFEND_TXTune() {
	gptrRFENDReg[1] &= 0xfffffeff;
	gptrRFENDReg[10] &= 0xffffefff;
	gptrRFENDReg[11] &= 0xffffffef;
	gptrRFENDReg[2] |= 0x20000;
	gptrRFENDReg[1] |= 0x10;

	// 2401 MHz
	gptrRFENDReg[1] &= 0xfffffffe;
	gptrRFENDReg[14] = gptrRFENDReg[14] & 0xfffe00ff | 0xbf00;
	gptrRFENDReg[1] |= 1;
	RFEND_TxTuneWait();
	uint8_t nCO2401 = (uint8_t)gptrRFENDReg[36] & 0x3f;
	uint8_t nGA2401 = (uint8_t)(gptrRFENDReg[37] >> 10) & 0x7f;

	// 2480 MHz
	gptrRFENDReg[1] &= 0xfffffffe;
	gptrRFENDReg[14] = gptrRFENDReg[14] & 0xfffe00ff | 0xe700;
	gptrRFENDReg[1] |= 1;
	RFEND_TxTuneWait();
	uint8_t nCO2480 = (uint8_t)gptrRFENDReg[36] & 0x3f;
	uint8_t nGA2480 = (uint8_t)(gptrRFENDReg[37] >> 10) & 0x7f;

	// 2440 MHz
	gptrRFENDReg[1] &= 0xfffffffe;
	gptrRFENDReg[14] = gptrRFENDReg[14] & 0xfffe00ff | 0xd300;
	gptrRFENDReg[1] |= 1;
	RFEND_TxTuneWait();
	uint8_t nCO2440 = (uint8_t)gptrRFENDReg[36] & 0x3f;
	uint8_t nGA2440 = (uint8_t)(gptrRFENDReg[37] >> 10) & 0x7f;

	uint32_t dCO0140 = nCO2401 - nCO2440;
	gptrRFENDReg[40] = gptrRFENDReg[40] & 0xfffffff0 | dCO0140 & 0xf;
	gptrRFENDReg[40] = ((int)(dCO0140 * 0x26) / 0x27 & 0xfU) << 4 | gptrRFENDReg[40] & 0xffffff0f;
	gptrRFENDReg[40] = ((int)(dCO0140 * 0x25) / 0x27 & 0xfU) << 8 | gptrRFENDReg[40] & 0xfffff0ff;
	gptrRFENDReg[40] = ((int)(dCO0140 * 0x24) / 0x27 & 0xfU) << 0xc | gptrRFENDReg[40] & 0xffff0fff;
	gptrRFENDReg[40] = ((int)(dCO0140 * 0x23) / 0x27 & 0xfU) << 0x10 | gptrRFENDReg[40] & 0xfff0ffff;
	gptrRFENDReg[40] = ((int)(dCO0140 * 0x22) / 0x27 & 0xfU) << 0x14 | gptrRFENDReg[40] & 0xff0fffff;
	gptrRFENDReg[40] = ((int)(dCO0140 * 0x21) / 0x27 & 0xfU) << 0x18 | gptrRFENDReg[40] & 0xf0ffffff;
	gptrRFENDReg[40] = gptrRFENDReg[40] & 0xfffffff | (int)(dCO0140 * 0x20) / 0x27 << 0x1c;

	gptrRFENDReg[41] = gptrRFENDReg[41] & 0xfffffff0 | (int)(dCO0140 * 0x1f) / 0x27 & 0xfU;
	gptrRFENDReg[41] = ((int)(dCO0140 * 0x1e) / 0x27 & 0xfU) << 4 | gptrRFENDReg[41] & 0xffffff0f;
	gptrRFENDReg[41] = ((int)(dCO0140 * 0x1d) / 0x27 & 0xfU) << 8 | gptrRFENDReg[41] & 0xfffff0ff;
	gptrRFENDReg[41] = ((int)(dCO0140 * 0x1c) / 0x27 & 0xfU) << 0xc | gptrRFENDReg[41] & 0xffff0fff;
	gptrRFENDReg[41] = ((int)(dCO0140 * 0x1b) / 0x27 & 0xfU) << 0x10 | gptrRFENDReg[41] & 0xfff0ffff;
	gptrRFENDReg[41] = ((int)(dCO0140 * 0x1a) / 0x27 & 0xfU) << 0x14 | gptrRFENDReg[41] & 0xff0fffff;
	gptrRFENDReg[41] = ((int)(dCO0140 * 0x19) / 0x27 & 0xfU) << 0x18 | gptrRFENDReg[41] & 0xf0ffffff;
	gptrRFENDReg[41] = gptrRFENDReg[41] & 0xfffffff | (int)(dCO0140 * 0x18) / 0x27 << 0x1c;

	gptrRFENDReg[42] = gptrRFENDReg[42] & 0xfffffff0 | (int)(dCO0140 * 0x17) / 0x27 & 0xfU;
	gptrRFENDReg[42] = ((int)(dCO0140 * 0x16) / 0x27 & 0xfU) << 4 | gptrRFENDReg[42] & 0xffffff0f;
	gptrRFENDReg[42] = ((int)(dCO0140 * 0x15) / 0x27 & 0xfU) << 8 | gptrRFENDReg[42] & 0xfffff0ff;
	gptrRFENDReg[42] = ((int)(dCO0140 * 0x14) / 0x27 & 0xfU) << 0xc | gptrRFENDReg[42] & 0xffff0fff;
	gptrRFENDReg[42] = ((int)(dCO0140 * 0x13) / 0x27 & 0xfU) << 0x10 | gptrRFENDReg[42] & 0xfff0ffff;
	gptrRFENDReg[42] = ((int)(dCO0140 * 0x12) / 0x27 & 0xfU) << 0x14 | gptrRFENDReg[42] & 0xff0fffff;
	gptrRFENDReg[42] = ((int)(dCO0140 * 0x11) / 0x27 & 0xfU) << 0x18 | gptrRFENDReg[42] & 0xf0ffffff;
	gptrRFENDReg[42] = gptrRFENDReg[42] & 0xfffffff | (int)(dCO0140 * 0x10) / 0x27 << 0x1c;

	gptrRFENDReg[43] = gptrRFENDReg[43] & 0xfffffff0 | (int)(dCO0140 * 0xf) / 0x27 & 0xfU;
	gptrRFENDReg[43] = ((int)(dCO0140 * 0xe) / 0x27 & 0xfU) << 4 | gptrRFENDReg[43] & 0xffffff0f;
	gptrRFENDReg[43] = gptrRFENDReg[43] & 0xfffff0ff | ((int)dCO0140 / 3 & 0xfU) << 8;
	gptrRFENDReg[43] = ((int)(dCO0140 * 0xc) / 0x27 & 0xfU) << 0xc | gptrRFENDReg[43] & 0xffff0fff;
	gptrRFENDReg[43] = ((int)(dCO0140 * 0xb) / 0x27 & 0xfU) << 0x10 | gptrRFENDReg[43] & 0xfff0ffff;
	gptrRFENDReg[43] = ((int)(dCO0140 * 10) / 0x27 & 0xfU) << 0x14 | gptrRFENDReg[43] & 0xff0fffff;
	gptrRFENDReg[43] = ((int)(dCO0140 * 9) / 0x27 & 0xfU) << 0x18 | gptrRFENDReg[43] & 0xf0ffffff;
	gptrRFENDReg[43] = gptrRFENDReg[43] & 0xfffffff | (int)(dCO0140 * 8) / 0x27 << 0x1c;

	gptrRFENDReg[44] = gptrRFENDReg[44] & 0xfffffff0 | (int)(dCO0140 * 7) / 0x27 & 0xfU;
	gptrRFENDReg[44] = ((int)(dCO0140 * 6) / 0x27 & 0xfU) << 4 | gptrRFENDReg[44] & 0xffffff0f;
	gptrRFENDReg[44] = ((int)(dCO0140 * 5) / 0x27 & 0xfU) << 8 | gptrRFENDReg[44] & 0xfffff0ff;
	gptrRFENDReg[44] = ((int)(dCO0140 * 4) / 0x27 & 0xfU) << 0xc | gptrRFENDReg[44] & 0xffff0fff;
	gptrRFENDReg[44] = gptrRFENDReg[44] & 0xfff0ffff | ((int)dCO0140 / 0xd & 0xfU) << 0x10;
	gptrRFENDReg[44] = ((int)(dCO0140 * 2) / 0x27 & 0xfU) << 0x14 | gptrRFENDReg[44] & 0xff0fffff;
	gptrRFENDReg[44] = gptrRFENDReg[44] & 0xf0ffffff | ((int)dCO0140 / 0x27 & 0xfU) << 0x18;
	gptrRFENDReg[44] = gptrRFENDReg[44] & 0xfffffff;

	uint32_t dCO4080 = nCO2440 - nCO2480;
	gptrRFENDReg[45] = gptrRFENDReg[45] & 0xfffffff0 | dCO4080 / 0x28 & 0xfU;
	gptrRFENDReg[45] = gptrRFENDReg[45] & 0xffffff0f | (dCO4080 / 0x14 & 0xfU) << 4;
	gptrRFENDReg[45] = gptrRFENDReg[45] & 0xfffff0ff | ((dCO4080 * 3) / 0x28 & 0xfU) << 8;
	gptrRFENDReg[45] = gptrRFENDReg[45] & 0xffff0fff | (dCO4080 / 10 & 0xfU) << 0xc;
	gptrRFENDReg[45] = gptrRFENDReg[45] & 0xfff0ffff | (dCO4080 / 8 & 0xfU) << 0x10;
	gptrRFENDReg[45] = gptrRFENDReg[45] & 0xff0fffff | ((dCO4080 * 6) / 0x28 & 0xfU) << 0x14;
	gptrRFENDReg[45] = gptrRFENDReg[45] & 0xf0ffffff | ((dCO4080 * 7) / 0x28 & 0xfU) << 0x18;
	gptrRFENDReg[45] = gptrRFENDReg[45] & 0xfffffff | dCO4080 / 5 << 0x1c;

	gptrRFENDReg[46] = gptrRFENDReg[46] & 0xfffffff0 | (dCO4080 * 9) / 0x28 & 0xfU;
	gptrRFENDReg[46] = gptrRFENDReg[46] & 0xffffff0f | (dCO4080 / 4 & 0xfU) << 4;
	gptrRFENDReg[46] = gptrRFENDReg[46] & 0xfffff0ff | ((dCO4080 * 0xb) / 0x28 & 0xfU) << 8;
	gptrRFENDReg[46] = gptrRFENDReg[46] & 0xffff0fff | ((dCO4080 * 0xc) / 0x28 & 0xfU) << 0xc;
	gptrRFENDReg[46] = gptrRFENDReg[46] & 0xfff0ffff | ((dCO4080 * 0xd) / 0x28 & 0xfU) << 0x10;
	gptrRFENDReg[46] = gptrRFENDReg[46] & 0xff0fffff | ((dCO4080 * 0xe) / 0x28 & 0xfU) << 0x14;
	gptrRFENDReg[46] = gptrRFENDReg[46] & 0xf0ffffff | ((dCO4080 * 0xf) / 0x28 & 0xfU) << 0x18;
	gptrRFENDReg[46] = gptrRFENDReg[46] & 0xfffffff | (dCO4080 * 0x10) / 0x28 << 0x1c;

	gptrRFENDReg[47] = gptrRFENDReg[47] & 0xfffffff0 | (dCO4080 * 0x11) / 0x28 & 0xfU;
	gptrRFENDReg[47] = gptrRFENDReg[47] & 0xffffff0f | ((dCO4080 * 0x12) / 0x28 & 0xfU) << 4;
	gptrRFENDReg[47] = gptrRFENDReg[47] & 0xfffff0ff | ((dCO4080 * 0x13) / 0x28 & 0xfU) << 8;
	gptrRFENDReg[47] = gptrRFENDReg[47] & 0xffff0fff | (dCO4080 / 2 & 0xfU) << 0xc;
	gptrRFENDReg[47] = gptrRFENDReg[47] & 0xfff0ffff | ((dCO4080 * 0x15) / 0x28 & 0xfU) << 0x10;
	gptrRFENDReg[47] = gptrRFENDReg[47] & 0xff0fffff | ((dCO4080 * 0x16) / 0x28 & 0xfU) << 0x14;
	gptrRFENDReg[47] = gptrRFENDReg[47] & 0xf0ffffff | ((dCO4080 * 0x17) / 0x28 & 0xfU) << 0x18;
	gptrRFENDReg[47] = gptrRFENDReg[47] & 0xfffffff | (dCO4080 * 0x18) / 0x28 << 0x1c;

	gptrRFENDReg[48] = gptrRFENDReg[48] & 0xfffffff0 | (dCO4080 * 0x19) / 0x28 & 0xfU;
	gptrRFENDReg[48] = gptrRFENDReg[48] & 0xffffff0f | ((dCO4080 * 0x1a) / 0x28 & 0xfU) << 4;
	gptrRFENDReg[48] = gptrRFENDReg[48] & 0xfffff0ff | ((dCO4080 * 0x1b) / 0x28 & 0xfU) << 8;
	gptrRFENDReg[48] = gptrRFENDReg[48] & 0xffff0fff | ((dCO4080 * 0x1c) / 0x28 & 0xfU) << 0xc;
	gptrRFENDReg[48] = gptrRFENDReg[48] & 0xfff0ffff | ((dCO4080 * 0x1d) / 0x28 & 0xfU) << 0x10;
	gptrRFENDReg[48] = gptrRFENDReg[48] & 0xff0fffff | ((dCO4080 * 0x1e) / 0x28 & 0xfU) << 0x14;
	gptrRFENDReg[48] = gptrRFENDReg[48] & 0xf0ffffff | ((dCO4080 * 0x1f) / 0x28 & 0xfU) << 0x18;
	gptrRFENDReg[48] = gptrRFENDReg[48] & 0xfffffff | (dCO4080 * 0x20) / 0x28 << 0x1c;

	gptrRFENDReg[49] = gptrRFENDReg[49] & 0xfffffff0 | (dCO4080 * 0x21) / 0x28 & 0xfU;
	gptrRFENDReg[49] = gptrRFENDReg[49] & 0xffffff0f | ((dCO4080 * 0x22) / 0x28 & 0xfU) << 4;
	gptrRFENDReg[49] = gptrRFENDReg[49] & 0xfffff0ff | ((dCO4080 * 0x23) / 0x28 & 0xfU) << 8;
	gptrRFENDReg[49] = gptrRFENDReg[49] & 0xffff0fff | ((dCO4080 * 0x24) / 0x28 & 0xfU) << 0xc;
	gptrRFENDReg[49] = gptrRFENDReg[49] & 0xfff0ffff | ((dCO4080 * 0x25) / 0x28 & 0xfU) << 0x10;
	gptrRFENDReg[49] = gptrRFENDReg[49] & 0xff0fffff | ((dCO4080 * 0x26) / 0x28 & 0xfU) << 0x14;
	gptrRFENDReg[49] = ((dCO4080 * 0x27) / 0x28 & 0xfU) << 0x18 | gptrRFENDReg[49] & 0xf0ffffff;
	gptrRFENDReg[49] = gptrRFENDReg[49] & 0xfffffff | dCO4080 * 0x10000000;

	gptrRFENDReg[50] = (dCO4080 * 0x29) / 0x28 & 0xfU | gptrRFENDReg[50] & 0xfffffff0;
	gptrRFENDReg[50] = ((dCO4080 * 0x2a) / 0x28 & 0xfU) << 4 | gptrRFENDReg[50] & 0xffffff0f;

	uint32_t dGA4080 = nGA2440 - nGA2480;
	gptrRFENDReg[50] = gptrRFENDReg[50] & 0xfffff0ff | ((dGA4080 * 10) / dCO4080 & 0xfU | 8) << 8;
	gptrRFENDReg[50] = gptrRFENDReg[50] & 0xffff0fff | ((dGA4080 * 9) / dCO4080 & 0xfU | 8) << 0xc;
	gptrRFENDReg[50] = gptrRFENDReg[50] & 0xfff0ffff | ((dGA4080 * 8) / dCO4080 & 0xfU | 8) << 0x10;
	gptrRFENDReg[50] = gptrRFENDReg[50] & 0xff0fffff | ((dGA4080 * 7) / dCO4080 & 0xfU | 8) << 0x14;
	gptrRFENDReg[50] = gptrRFENDReg[50] & 0xf0ffffff | ((dGA4080 * 6) / dCO4080 & 0xfU | 8) << 0x18;
	gptrRFENDReg[50] = gptrRFENDReg[50] & 0xfffffff | ((dGA4080 * 5) / dCO4080 | 8U) << 0x1c;
	gptrRFENDReg[51] = gptrRFENDReg[51] & 0xfffffff0 | (dGA4080 * 4) / dCO4080 & 0xfU | 8;
	gptrRFENDReg[51] = gptrRFENDReg[51] & 0xffffff0f | ((dGA4080 * 3) / dCO4080 & 0xfU | 8) << 4;
	gptrRFENDReg[51] = gptrRFENDReg[51] & 0xfffff0ff | ((dGA4080 * 2) / dCO4080 & 0xfU | 8) << 8;
	gptrRFENDReg[51] = (dGA4080 / dCO4080 & 0xfU | 8) << 0xc | gptrRFENDReg[51] & 0xffff0fff;
	gptrRFENDReg[51] = gptrRFENDReg[51] & 0xfff0ffff;

	uint32_t dGA0140 = nGA2401 - nGA2440;
	gptrRFENDReg[51] = gptrRFENDReg[51] & 0xff0fffff | (dGA0140 / (int)dCO0140 & 0xfU) << 0x14;
	gptrRFENDReg[51] = ((dGA0140 * 2) / (int)dCO0140 & 0xfU) << 0x18 | gptrRFENDReg[51] & 0xf0ffffff;
	gptrRFENDReg[51] = gptrRFENDReg[51] & 0xfffffff | (dGA0140 * 3) / (int)dCO0140 << 0x1c;
	gptrRFENDReg[52] = gptrRFENDReg[52] & 0xfffffff0 | (dGA0140 * 4) / (int)dCO0140 & 0xfU;
	gptrRFENDReg[52] = gptrRFENDReg[52] & 0xffffff0f | ((dGA0140 * 5) / (int)dCO0140 & 0xfU) << 4;
	gptrRFENDReg[52] = gptrRFENDReg[52] & 0xfffff0ff | ((dGA0140 * 6) / (int)dCO0140 & 0xfU) << 8;
	gptrRFENDReg[52] = gptrRFENDReg[52] & 0xffff0fff | ((dGA0140 * 7) / (int)dCO0140 & 0xfU) << 0xc;
	gptrRFENDReg[52] = gptrRFENDReg[52] & 0xfff0ffff | ((dGA0140 * 8) / (int)dCO0140 & 0xfU) << 0x10;
	gptrRFENDReg[52] = gptrRFENDReg[52] & 0xff0fffff | ((dGA0140 * 9) / (int)dCO0140 & 0xfU) << 0x14;
	gptrRFENDReg[52] = ((dGA0140 * 10) / (int)dCO0140 & 0xfU) << 0x18 | gptrRFENDReg[52] & 0xf0ffffff;

	gptrRFENDReg[1] = gptrRFENDReg[1] & 0xffffffef;
	gptrRFENDReg[1] = gptrRFENDReg[1] & 0xfffffffe;
	gptrRFENDReg[10] = gptrRFENDReg[10] | 0x1000;
	gptrRFENDReg[11] = gptrRFENDReg[11] | 0x10;
	gptrRFENDReg[14] = gptrRFENDReg[14] & 0xffffffc0 | nCO2440 & 0x3f;
	gptrRFENDReg[14] = ((nGA2440 & 0x7f) << 0x18) | (gptrRFENDReg[14] & 0x80ffffff);

	// FTune
	gptrRFENDReg[1] |= 0x100;
}

void RegInit() {
	gptrBBReg[0] = gptrBBReg[0] & 0xfffffcff | 0x280;
	gptrRFENDReg[2] |= 0x330000;
	gptrLLEReg[20] = 0x30558;
	RFEND_TXTune();
	gptrBBReg[0] = gptrBBReg[0] & 0xfffffcff | 0x100;
	gptrRFENDReg[2] &= 0xffcdffff;
	gptrLLEReg[20] = 0x30000;
	gBleIPPara.par7 = 0; // DAT_20003b77 = 0;
}

void IPCoreInit(uint8_t TxPower) {
	dtmFlag = 0;
	gPaControl = 0;
	gptrBBReg = (uint32_t *)0x4000c100;
	gptrLLEReg = (uint32_t *)0x4000c200;
	gptrAESReg = (uint32_t *)0x4000c300;
	gptrRFENDReg = (uint32_t *)0x4000d000;
	gBleIPPara.par7 = 1; // DAT_20003b77 = 1;
	gBleIPPara.par16 = (uint32_t)MEM_BUF;
	gBleIPPara.par15 = (uint32_t)MEM_BUF + 0x110;
	DevInit(TxPower);
	RegInit();
	PFIC->IPRIOR[0x15] |= 0x80;
	PFIC->IENR[0] = 0x200000;
}

void DevSetChannel(uint8_t channel) {
	gptrRFENDReg[11] &= 0xfffffffd;
	gptrBBReg[0] = gptrBBReg[0] & 0xffffff80 | channel & 0x7f;
	if(LLE_MODE_BASIC & 2) {
		gptrBBReg[0] = gptrBBReg[0] & 0xffffff80 | gptrBBReg[0] & 0x7f | 0x40;
	}
}

void PHYSetTxMode(int32_t mode, size_t len) {
	if(gPaControl) {
		*gPaControl[4] |= *gPaControl[5];
		*gPaControl[0] |= *gPaControl[2];
	}

	int32_t idx = 0;
	if(mode == 1) {
		gptrBBReg[0] |= 0x80;
		idx = (len +11) *4;
	}
	else {
		gptrBBReg[0] &= 0xffffff7f;;
		idx = (len +10) *8;
	}

	gptrLLEReg[3] &= 0xfffdffff;
	__asm__ volatile("fence.i");
	gptrLLEReg[2] = 0x20000;

	gBleIPPara.par4 = 0x80;
	gptrLLEReg[25] = (idx + gBleIPPara.par10 + 0x9e) *2;
}

void HopChannel() {
	uint32_t chan = rfInfo.par7 + rfConfig.HopIndex & 0x1f;
	uint32_t cnt = 0;
	rfConfig.Channel = chan;
	rfInfo.par7 = chan;
	if(rfConfig.ChannelMap >> chan & 1) {
		for(uint8_t i = 0; i < 0x20; i++) {
			if ((rfConfig.ChannelMap >> (i & 0x1f) & 1) != 0) {
				if (chan % (uint32_t)rfInfo.par11 == cnt) {
					rfConfig.Channel = i;
					return;
				}
			cnt = cnt + 1 & 0xff;
			}
		}
	}
}

uint32_t FrequencyHopper() {
	uint32_t res = 0;
	uint32_t period = 0;
	uint32_t getClockCB = (*fnGetClockCBs)();
	int32_t clockCB = -(rfInfo.par5);
	if(rfInfo.par5 != getClockCB) {
		if(getClockCB < rfInfo.par5) {
			clockCB = bleClock_t.par1 - rfInfo.par5;
		}
		res = getClockCB + clockCB >> 5;
		if(rfConfig.HopPeriod <= res) {
			do {
				HopChannel();
				period = rfConfig.HopPeriod;
				rfInfo.par5 += period * 0x20;
				res -= period;
				if(bleClock_t.par1 <= rfInfo.par5) {
					rfInfo.par5 -= bleClock_t.par1;
				}
			} while(period <= res);
		}
	}
	return res;
}

void txProcess() {
	if((gBleIPPara.par3 & 1) == 0) {
		if(gptrLLEReg[25]) {
			if(gBleIPPara.par7 == 0x03) {
				tmosSign = 1;
			}
			return;
		}
	}
	else {
		gBleIPPara.par3 = 0;
	}
	gBleIPPara.par4 = 0;
}

void Advertise(uint8_t adv[], size_t len, uint8_t channel) {
	if(rfInfo.par6 & 2) {
		uint32_t hopper = 0;
		do {
			do {
				hopper = FrequencyHopper();
			} while(hopper < 16);
		} while((uint32_t)rfConfig.HopPeriod * 0x20 - 0x10 < hopper);
		gptrLLEReg[25] = 0x50;
	}
	gptrBBReg[11] = gptrBBReg[11] & 0xfffffffc | 1;

	DevSetChannel(channel);

	gptrBBReg[0] &= 0xfffffeff;
	gptrRFENDReg[2] |= 0x330000;
	gptrLLEReg[20] = 0x30258;
	gptrBBReg[2] = 0x8E89BED6;
	gptrBBReg[1] = 0x555555;
	gptrLLEReg[1] = gptrLLEReg[1] & 0xfffffffe | LLE_MODE_BASIC & 1;

	*(uint8_t*)(gBleIPPara.par15) = 0x02; //TxPktType
	*(uint8_t*)(gBleIPPara.par15 +1) = len ;
	memcpy((uint8_t*)(gBleIPPara.par15 +2), adv, len);

	gptrLLEReg[30] = gBleIPPara.par15;
	gBleIPPara.par2 = 0;
	gBleIPPara.par3 = 0;
	gBleIPPara.par4 = 0;
	gBleIPPara.par7 = 0x03;

	if(rfInfo.par6 & 2) {
		while(gptrLLEReg[25]);
		uint32_t getClockCB = (*fnGetClockCBs)();
		int32_t clockCB = -(rfInfo.par5);
		if(getClockCB < rfInfo.par5) {
			clockCB = bleClock_t.par1 - rfInfo.par5;
		}

		len += 2;
		*(uint8_t*)(gBleIPPara.par12 +1) = len;
		*(uint8_t*)(gBleIPPara.par12 +len) = 0;
		*(uint8_t*)(gBleIPPara.par12 +len +1) = (uint8_t)(getClockCB + clockCB >> 3);
	}

	PHYSetTxMode(LLE_MODE_BASIC >> 4 & 3, len);
	txProcess();

	gptrBBReg[0] |= 0x800000;
	gptrBBReg[11] &= 0xfffffffc;
	gBleIPPara.par1 = 0;
	gBleIPPara.par5 = 0;
	gptrLLEReg[0] = 2;

	RF_Wait_Tx_End();
}

void RF_2G4StatusCallBack(uint8_t sta, uint8_t crc, uint8_t *rxBuf) {
	switch(sta) {
	case TX_MODE_TX_FINISH:
	case TX_MODE_TX_FAIL:
		tx_end_flag = TRUE;
		break;
	default:
		break;
	}
}

int main(void) {
	PWR_DCDCCfg(ENABLE);
	SetSysClock(CLK_SOURCE_PLL_60MHz);
	GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeOut_PP_5mA);
	HAL_TimeInit();
	HAL_SleepInit();

	g_LLE_IRQLibHandlerLocation = (uint32_t)LLE_IRQLibHandler; // for ble_task_scheduler.S
	ble.MEMAddr = (uint32_t)MEM_BUF;
	ble.MEMLen = (uint32_t)BLE_MEMHEAP_SIZE;
	TMOS_Init(); // for the BLE clocks R told me

	IPCoreInit(TXPOWER_MINUS_3_DBM);

	rfConfig.rfStatusCB = RF_2G4StatusCallBack;

	uint8_t adv[] = {0x66, 0x55, 0x44, 0x33, 0x22, 0xd1, // MAC (reversed)
					0x1e, 0xff, 0x4c, 0x00, 0x12, 0x19, 0x00, // Apple FindMy stuff
					0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xef, 0xfe, 0xdd,0xcc, // key
					0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // more key
					0x00, 0x00}; // status byte and one more

	uint8_t adv_channels[] = {37,38,39};
	for(int c = 0; c < sizeof(adv_channels); c++) {
		Advertise(adv, sizeof(adv), adv_channels[c]);
	}

	GPIOA_ResetBits(GPIO_Pin_8);
	CH59x_LowPower(MS_TO_RTC(30));
	GPIOA_SetBits(GPIO_Pin_8);

	CH59x_LowPower(MS_TO_RTC(1000));
}
