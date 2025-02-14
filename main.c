#include "HAL.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4]; // try to get rid of this

volatile uint8_t tx_end_flag = 0;
rfConfig_t rf_Config = {0};

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
	CH59x_BLEInit();
	HAL_Init();
	RF_RoleInit();

	rf_Config.accessAddress = 0x8E89BED6;
	rf_Config.CRCInit = 0x555555;
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
