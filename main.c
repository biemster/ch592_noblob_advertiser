#include "HAL.h"

extern uint8_t dtmFlag;
extern uint32_t gPaControl;
extern uint32_t *gptrAESReg;
extern uint32_t *gptrLLEReg;
extern uint32_t *gptrRFENDReg;
extern uint32_t *gptrBBReg;

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4]; // try to get rid of this
uint32_t g_LLE_IRQLibHandlerLocation;
volatile uint8_t tx_end_flag = 0;
rfConfig_t rf_Config = {0};
extern bleConfig_t ble;

struct bleIPPara {
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
	uint32_t par10;
	uint32_t par11;
	uint32_t par12;
	uint32_t par13;
};
extern struct bleIPPara gBleIPPara;

__HIGH_CODE
__attribute__((noinline))
void RF_Wait_Tx_End() {
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

void DevInit() {
	*(gptrLLEReg +3) = 0x1f000f;
	*(gptrLLEReg +5) = 0x8c;
	*(gptrLLEReg +6) = 0x78;
	*(gptrLLEReg +7) = 0x76;
	*(gptrLLEReg +8) = 0xffffffff;
	*(gptrLLEReg +9) = 0x8c;
	*(gptrLLEReg +11) = 0x6e;
	*(gptrLLEReg +13) = 0x8c;
	*(gptrLLEReg +15) = 0x6e;
	*(gptrLLEReg +17) = 0x8c;
	*(gptrLLEReg +19) = 0x76;
	*(gptrLLEReg +21) = 0x14;
	*(gptrLLEReg +31) = ble.MEMAddr;

	*(gptrRFENDReg +10) = 0x480;
	*(gptrRFENDReg +12) = *(gptrRFENDReg +12) & 0x8fffffff | 0x10077700;
	*(gptrRFENDReg +15) = *(gptrRFENDReg +15) & 0x18ff0fff | 0x42005000;
	*(gptrRFENDReg +19) &= 0xfffffff8;
	*(gptrRFENDReg +21) = *(gptrRFENDReg +21) & 0xfffffff0 | 9;
	*(gptrRFENDReg +23) &= 0xff88ffff;

	*(gptrBBReg) |= 0x800000;
	*(gptrBBReg +13) = 0x50;

	*(gptrBBReg +11) |= 0x80000000;
	*(gptrBBReg +11) = ((ble.TxPower & 0x3f) << 0x19) | (*(gptrBBReg +11) & 0x81ffffff);
	uint32_t uVar3 = 0x1000000;
	uint32_t uVar4 = *(gptrRFENDReg +23) & 0xf8ffffff;
	if(ble.TxPower < 29) {
		/* uVar3 and uVar4 are initialized properly already */
	}
	else if(ble.TxPower < 35) {
		uVar3 = 0x3000000;
	}
	else if(ble.TxPower < 59) {
		uVar3 = 0x5000000;
	}
	else {
		uVar4 = *(gptrRFENDReg +23);
		uVar3 = 0x7000000;
	}
	*(gptrRFENDReg +23) = uVar4 | uVar3;
	*(gptrBBReg +4) = *(gptrBBReg +4) & 0xffffffc0 | 0xe;
}

void RegInit() {
	*gptrBBReg = *gptrBBReg & 0xfffffcff | 0x280;
	*(gptrRFENDReg +2) |= 0x330000;
	*(gptrLLEReg +20) = 0x30558;
	RFEND_TXCtune();
	RFEND_TXFtune();
	*gptrBBReg = *gptrBBReg & 0xfffffcff | 0x100;
	*(gptrRFENDReg +2) &= 0xffcdffff;
	*(gptrLLEReg +20) = 0x30000;
	gBleIPPara.par7 = 0; // DAT_20003b77 = 0;
}

void IPCoreInit() {
	dtmFlag = 0;
	gPaControl = 0;
	gptrBBReg = (uint32_t *)0x4000c100;
	gptrLLEReg = (uint32_t *)0x4000c200;
	gptrAESReg = (uint32_t *)0x4000c300;
	gptrRFENDReg = (uint32_t *)0x4000d000;
	gBleIPPara.par7 = 1; // DAT_20003b77 = 1;
	gBleIPPara.par13 = ble.MEMAddr; // DAT_20003b88 = ble;
	gBleIPPara.par12 = ble.MEMAddr + 0x110; // DAT_20003b84 = ble + 0x110;
	DevInit();
	RegInit();
	PFIC->IPRIOR[0x15] |= 0x80;
	PFIC->IENR[0] = 0x200000;
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

void send_adv(uint8_t adv[], size_t len, uint8_t channel) {
	rf_Config.Channel = channel;
	uint8_t state = RF_Config(&rf_Config);
	tx_end_flag = FALSE;
	if(!RF_Tx(adv, len, 0x02, 0xFF)) {
		RF_Wait_Tx_End();
	}
}

int main(void) {
	PWR_DCDCCfg(ENABLE);
	SetSysClock(CLK_SOURCE_PLL_60MHz);
	GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeOut_PP_5mA);
	HAL_TimeInit();
	HAL_SleepInit();

	g_LLE_IRQLibHandlerLocation = (uint32_t)LLE_IRQLibHandler;
	bleConfig_t blecfg = {0};
	blecfg.MEMAddr = (uint32_t)MEM_BUF;
	blecfg.MEMLen = (uint32_t)BLE_MEMHEAP_SIZE;
	BLE_LibInit(&blecfg);

	IPCoreInit();

	rf_Config.accessAddress = 0x8E89BED6; // gptrBBReg[2]
	rf_Config.CRCInit = 0x555555; // gptrBBReg[1]
	rf_Config.LLEMode = LLE_MODE_BASIC;
	rf_Config.rfStatusCB = RF_2G4StatusCallBack;
	uint8_t state = RF_Config(&rf_Config);

	uint8_t adv[] = {0x66, 0x55, 0x44, 0x33, 0x22, 0xd1, // MAC (reversed)
					0x1e, 0xff, 0x4c, 0x00, 0x12, 0x19, 0x00, // Apple FindMy stuff
					0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xef, 0xfe, 0xdd,0xcc, // key
					0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // more key
					0x00, 0x00}; // status byte and one more

	send_adv(adv, sizeof(adv), 37);
	send_adv(adv, sizeof(adv), 38);
	send_adv(adv, sizeof(adv), 39);

	GPIOA_ResetBits(GPIO_Pin_8);
	CH59x_LowPower(MS_TO_RTC(30));
	GPIOA_SetBits(GPIO_Pin_8);

	CH59x_LowPower(MS_TO_RTC(1000));
}
