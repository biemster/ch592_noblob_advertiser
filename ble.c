#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define TX_MODE_TX_FINISH   0x01  //!< basic or auto tx mode sends data successfully
#define TX_MODE_TX_FAIL     0x11  //!< basic or auto tx mode fail to send data and enter idle state
#define LLE_MODE_BASIC      0

uint32_t *gptrLLEReg;
uint32_t volatile *gptrRFENDReg; // needs volatile, otherwise part of the tuning process is optimized out
uint32_t *gptrBBReg;

__attribute__((aligned(4))) uint32_t LLE_BUF[0x110];
__attribute__((aligned(4))) uint8_t  ADV_BUF[40]; // for the advertisement, which is 37 bytes + 2 header bytes
volatile uint8_t tx_end_flag = 0;

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

typedef void (*pfnRFStatusCB_t)( uint8_t sta, uint8_t rsr, uint8_t *rxBuf );
typedef struct tag_rf_config
{
    uint8_t LLEMode;                  //!< BIT0   0=basic, 1=auto def@LLE_MODE_TYPE
                                      //!< BIT1   0=whitening on, 1=whitening off def@LLE_WHITENING_TYPE
                                      //!< BIT4-5 00-1M  01-2M  10/11-resv def@LLE_PHY_TYPE
                                      //!< BIT6   0=data channel(0-39)
                                      //!<        1=rf frequency (2400000kHz-2483500kHz)
                                      //!< BIT7   0=the first byte of the receive buffer is rssi
                                      //!<        1=the first byte of the receive buffer is package type
    uint8_t Channel;                  //!< rf channel(0-39)
    uint32_t Frequency;               //!< rf frequency (2400000kHz-2483500kHz)
    uint32_t accessAddress;           //!< access address,32bit PHY address
    uint32_t CRCInit;                 //!< crc initial value
    pfnRFStatusCB_t rfStatusCB;       //!< status call back
    uint32_t ChannelMap;              //!< indicating  Used and Unused data channels.Every channel is represented with a
                                      //!< bit positioned as per the data channel index,The LSB represents data channel index 0
    uint8_t Resv;
    uint8_t HeartPeriod;              //!< The heart package interval shall be an integer multiple of 100ms
    uint8_t HopPeriod;                //!< hop period( T=32n*RTC clock ),default is 8
    uint8_t HopIndex;                 //!< indicate the hopIncrement used in the data channel selection algorithm,default is 17
    uint8_t RxMaxlen;                 //!< Maximum data length received in rf-mode(default 251)
    uint8_t TxMaxlen;                 //!< Maximum data length transmit in rf-mode(default 251)
} rfConfig_t;
rfConfig_t rfcfg = {0};

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
struct rfInfo_t rfinf;

__attribute__((interrupt))
void LLE_IRQHandler() {
	gptrLLEReg[2] = 0xffffffff; // STATUS
}

__attribute__((noinline))
void RF_Wait_Tx_End() {
	tx_end_flag = 0;
	uint32_t i = 0;
	while(!tx_end_flag) {
		i++;
		asm volatile ("nop\nnop");
		if(i > ((60*1000*1000) / 1000)) { // 60MHz clock
			tx_end_flag = 1;
		}
	}
}

void RF_2G4StatusCallBack(uint8_t sta, uint8_t crc, uint8_t *rxBuf) {
	switch(sta) {
	case TX_MODE_TX_FINISH:
	case TX_MODE_TX_FAIL:
		tx_end_flag = 1;
		break;
	default:
		break;
	}
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
	gptrLLEReg[31] = (uint32_t)LLE_BUF;

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
	// DAT_e000e053 = 0x14; // radio.h: NVIC->IDCFGR[3] = 0x14;
	// _DAT_e000e06c = 0x200016cf; // radio.h: NVIC->FIADDRR[3] = (uint32_t)(&BB_IRQLibFunction) | 1;
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
}

void BLECoreInit(uint8_t TxPower) {
	gptrBBReg = (uint32_t *)0x4000c100;
	gptrLLEReg = (uint32_t *)0x4000c200;
	gptrRFENDReg = (uint32_t *)0x4000d000;
	rfcfg.rfStatusCB = RF_2G4StatusCallBack;
	DevInit(TxPower);
	RegInit();
	NVIC->IPRIOR[0x15] |= 0x80;
	NVIC->IENR[0] = 0x200000;
}

void DevSetChannel(uint8_t channel) {
	gptrRFENDReg[11] &= 0xfffffffd;
	gptrBBReg[0] = gptrBBReg[0] & 0xffffff80 | channel & 0x7f;
	if(LLE_MODE_BASIC & 2) {
		gptrBBReg[0] = gptrBBReg[0] & 0xffffff80 | gptrBBReg[0] & 0x7f | 0x40;
	}
}

void PHYSetTxMode(int32_t mode, size_t len) {
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

	gptrLLEReg[25] = (idx + 0x9e) *2;
}

void HopChannel() {
	uint32_t chan = rfinf.par7 + rfcfg.HopIndex & 0x1f;
	uint32_t cnt = 0;
	rfcfg.Channel = chan;
	rfinf.par7 = chan;
	if(rfcfg.ChannelMap >> chan & 1) {
		for(uint8_t i = 0; i < 0x20; i++) {
			if ((rfcfg.ChannelMap >> (i & 0x1f) & 1) != 0) {
				if (chan % (uint32_t)rfinf.par11 == cnt) {
					rfcfg.Channel = i;
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
	if(rfcfg.HopPeriod <= res) {
		do {
			HopChannel();
			period = rfcfg.HopPeriod;
			rfinf.par5 += period * 0x20;
			res -= period;
		} while(period <= res);
	}
	return res;
}

void Advertise(uint8_t adv[], size_t len, uint8_t channel) {
	if(rfinf.par6 & 2) {
		uint32_t hopper = 0;
		do {
			do {
				hopper = FrequencyHopper();
			} while(hopper < 16);
		} while((uint32_t)rfcfg.HopPeriod * 0x20 - 0x10 < hopper);
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

	ADV_BUF[0] = 0x02; //TxPktType 0x00, 0x02, 0x06 seem to work, with only 0x02 showing up on the phone
	ADV_BUF[1] = len ;
	memcpy(&ADV_BUF[2], adv, len);

	gptrLLEReg[30] = (uint32_t)ADV_BUF;

	PHYSetTxMode(LLE_MODE_BASIC >> 4 & 3, len);

	gptrBBReg[0] |= 0x800000;
	gptrBBReg[11] &= 0xfffffffc;
	gptrLLEReg[0] = 2;

	RF_Wait_Tx_End();
}
