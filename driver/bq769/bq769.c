/*
 * Copyright (C) 2021-2099 PLKJ Development Team
 *
 * SPDX-License-Identifier: CC BY-NC 4.0
 // 分流电阻阻值, 单位: 欧 (例如 0.005 表示 5 mΩ)，用于根据电压降计算电流
 static float RsnsValue= 0.005;

 *
 * http://creativecommons.org/licenses/by-nc/4.0/
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * Change Logs:
 * Date           Author            Notes
 * 2022-6-5       逍遥吾皇          the first version
 * 2022-6-18      逍遥吾皇          (1)重新对三元锂电池SOC对应电压数据表排序
 *									(3)utils增加了二分查找算法用于开路SOC计算
 *									(2)修改了开路电压计算函数使之过程更清晰
 * 2022-7-2       逍遥吾皇          去掉查表方法,更新热敏电阻计算公式
 * 2023-1-5       逍遥吾皇          去掉了BQ769X0驱动的动态内存分配
*/


#include "bq769.h"
/* 警报处理函数声明：当外部中断（ALERT）触发时调用该函数处理各种报警位 */
static void BQ769X0_AlertyHandler(void);


#include <math.h>

#include "stm32f10x.h"
#include "soft_i2c.h"


#define DBG_TAG "bq76920"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

// 报警回调接口结构体：保存用户传入的各种报警处理函数指针
static BQ769X0_AlertOpsTypedef AlertOps;


/* ADC相关参数说明 -------------------------------------------------- */
/* ADC增益，单位 uV/LSB（浮点，供计算时使用） */
static float Gain = 0;
/* ADC增益（整数形式，直接用于整数运算） */
static int16_t iGain = 0;
/* ADC偏移量，寄存器读出为8位补码，这里保存为带符号的偏移（mV） */
static int8_t Adcoffset;

/* 温度采样模式：0 使用外部热敏电阻采样；1 使用IC内部温度传感器 */
static uint8_t TempSampleMode = 0;  // 0: 热敏电阻  1: IC内部温度


// 寄存器组：用于本地缓存要写入或刚读取的寄存器内容
static RegisterGroup Registers = {0};


// 18650 1C放电倍率是指电池以1小时时间放完额定容量
// 假如电池额定容量为2200mAh ,那么1C放电电流是2.2A
// 18650短路电流阈值一般为电池的5C放电速率
// 短路电流一般设置为10A（搜索别人实际测过的经验值）




// 放电短路保护阈值，选择44还是89是根据RSNS位配置的，为1则加倍阈值否则不加倍
// 假如SCD预设为10A 根据我的电路分流电阻规格是5mΩ
// 计算  	SCDThresh = 10A * 5mΩ = 50mV
// 向下取值应该取 SCD_THRESH_89mV_44mV  并且RSNS设置为0
// 取44mV 实际计算 44 / 5 = 8.8A 这才是真正的阈值保护电流
// 设置为56mV 放电短路阈值电流是11.2A
static const uint8_t SCDThresh = SCD_THRESH_89mV_44mV;




// 放电过流保护阈值，一般设置为2A
// 选择11还是22是根据RSNS位配置的，为1则加倍阈值否则不加倍
// 假如OCD预设为2.2A 根据我的电路分流电阻规格是5mΩ
// 计算  	OCDThresh = 2.2A * 5mΩ = 11mV
// 取值应该取 OCD_THRESH_22mV_11mV  并且RSNS设置为0
// 设置为8mV 放电过流阈值电流是1.6A
// 设置为14mV 放电过流阈值电流是2.8A
static const uint8_t OCDThresh = OCD_THRESH_22mV_11mV;

// OCD_THRESH_17mV_8mV  OCD_THRESH_22mV_11mV






/*
// 放电短路保护延时时间
static const uint8_t SCDDelay = SCD_DELAY_100us;

// 放电过流保护延时时间
static const uint8_t OCDDelay = OCD_DELAY_320ms;

// 过压保护延迟
static const uint8_t OVDelay = OV_DELAY_2s;

// 欠压保护延迟
static const uint8_t UVDelay = UV_DELAY_4s;




// 三元锂和磷酸铁锂区别看标称电压,三元锂:3.6V或3.7V						磷酸铁锂:3.2V
// 三元锂过充终止:4.2V  				磷酸铁锂过充终止:3.6V
// 三元锂过放截止:3.2V  				磷酸铁锂过放截止:2.5V
// 以上值只是行业参考，锂电池过充、过放值还得根据电池卖家给的值

// BQ芯片可配置过压保护阈值,范围3.15~4.7V
static const uint16_t  OVPThreshold = 4200;		

// BQ芯片可配置欠压保护阈值,范围1.58~3.1V
static const uint16_t	UVPThreshold = 2900;
*/




// 分流电阻阻值,单位毫欧
static float RsnsValue= 0.005;		




// 传感数据
BQ769X0_SampleDataTypedef BQ769X0_SampleData = {0};


static void BQ769X0_AlertyHandler(void);



/*********************************** GPIO *********************************************/
/*
static void BQ769X0_GpioInit(void)
{

}
*/

static void BQ769X0_TS1_SetOutMode(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    GPIO_InitStruct.GPIO_Pin = BQ769X0_TS1_Pin;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(BQ769X0_TS1_GPIOx, &GPIO_InitStruct);
}

static void BQ769X0_TS1_SetInMode(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
	
    GPIO_InitStruct.GPIO_Pin = BQ769X0_TS1_Pin;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(BQ769X0_TS1_GPIOx, &GPIO_InitStruct);
}

static void BQ769X0_ALERT_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	GPIO_InitStruct.GPIO_Pin = BQ769X0_ALERT_Pin;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPD;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(BQ769X0_ALERT_GPIOx, &GPIO_InitStruct);

	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource12);// 将GPIOB的Pin12连接到外部中断线12
	EXTI_InitTypeDef EXTI_InitStruct;
	EXTI_InitStruct.EXTI_Line = EXTI_Line12;
	EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Rising;
	EXTI_InitStruct.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStruct);

	NVIC_InitTypeDef NVIC_InitStruct;
	NVIC_InitStruct.NVIC_IRQChannel = BQ769X0_ALERT_EXIT_IRQ;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 3;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStruct);
}

void EXTI15_10_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line12) != RESET)
    {
		BQ769X0_AlertyHandler();
    }
    EXTI_ClearITPendingBit(EXTI_Line12);
}
/******************************************************************************************/


/************************************** utils **********************************************/


// 热敏电阻阻值换算成温度
static float TempChange(float	Rt)
{
	float temp = 0;

	// 热敏电阻在T2常温下的标称阻值,我买的是10K
	float Rp = 10000;

	// 该热敏电阻在开尔文温度下的,热敏电阻阻值为10K时对应的温度为25度
	float T2 = 273.15 + 25;

	// B值:3935、3950
	float Bx = 3950;

	// 开尔文温度值
	float Ka = 273.15;

	// 打印出热敏电阻的实时阻值可与购买链接的阻值与温度对应表对照查看
	//sprintf((char *)buffer, "%f", Rt);
	//BQ769X0_INFO("Rts value:%s", buffer);

	temp = 1 / (1 / T2 + log(Rt / Rp) / Bx)- Ka + 0.5;

	return temp;
}




// CRC8校验
//参数说明： ptr:指向要计算CRC的数据缓冲区的指针；len:数据长度；key:CRC计算的多项式常数
static uint8_t CRC8(uint8_t *ptr, uint8_t len, uint8_t key)
{
	uint8_t i, crc=0;
	
	while (len-- != 0)
	{
		for (i = 0x80; i != 0; i /= 2)
		{
			if ((crc & 0x80) != 0)
			{
				crc *= 2;
				crc ^= key;
			}
			else
			{
				crc *= 2;
			}

			if ((*ptr & i) != 0)
			{
				crc ^= key;
			}
		}
		ptr++;
	}
	return(crc);
}
/******************************************************************************************/


/* ----------------------------- 读写寄存器相关函数 -----------------------------
 * 下面是一组对BQ769X0芯片进行I2C读写的封装函数：
 * - BQ769X0_WriteRegisterByte:    写单字节寄存器（无CRC）
 * - BQ769X0_WriteRegisterByteWithCRC: 写单字节寄存器（带CRC协议）
 * - BQ769X0_WriteRegisterWordWithCRC: 写双字节寄存器（带CRC）
 * - BQ769X0_WriteBlockWithCRC:    批量写寄存器（每字节后跟CRC）
 * - BQ769X0_ReadRegisterByte:     读单字节寄存器（无CRC）
 * - BQ769X0_ReadRegisterByteWithCRC: 读单字节寄存器并校验CRC
 * - BQ769X0_ReadRegisterWordWithCRC:  读双字节寄存器并校验CRC
 * - BQ769X0_ReadBlockWithCRC:     批量读寄存器并按每字节CRC校验
 */

static bool BQ769X0_WriteRegisterByte(uint8_t Register, uint8_t data)
{
    uint8_t dataBuffer[2] = {Register, data};

	struct I2C_MessageTypeDef msg = {0};

	msg.addr = BQ769X0_I2C_ADDR;
	msg.flags = I2C_WR;	
	msg.buf = dataBuffer;
	msg.tLen = 2;
	
	/* 发送I2C写事务（设备地址 + 寄存器 + 数据），返回值为实际传输消息数 */
	if (I2C_TransferMessages(&i2c1, &msg, 1) != 1)
	{
		LOG_E("Write Register Byte Fail");
		return false;
	}

    return true;
}

static bool BQ769X0_WriteRegisterByteWithCRC(uint8_t Register, uint8_t data)
{
  uint8_t dataBuffer[4];
	struct I2C_MessageTypeDef msg = {0};

	dataBuffer[0] = BQ769X0_I2C_ADDR << 1;
	dataBuffer[1] = Register;
	dataBuffer[2] = data;	
	dataBuffer[3] = CRC8(dataBuffer, 3, CRC_KEY);

	msg.addr = BQ769X0_I2C_ADDR;
	msg.flags = I2C_WR;	
	msg.buf = dataBuffer + 1;
	msg.tLen = 3;

	/* 使用带CRC的写入协议：设备地址+寄存器+数据+CRC */
	if (I2C_TransferMessages(&i2c1, &msg, 1) != 1)
	{
		LOG_E("Write Register Byte With CRC Fail");
		return false;
	}

    return true;
}


static bool BQ769X0_WriteRegisterWordWithCRC(uint8_t Register, uint16_t data)
{
    uint8_t dataBuffer[6];
	struct I2C_MessageTypeDef msg = {0};

	dataBuffer[0] = BQ769X0_I2C_ADDR << 1;
	dataBuffer[1] = Register;
	dataBuffer[2] = LOW_BYTE(data);	
	dataBuffer[3] = CRC8(dataBuffer, 3, CRC_KEY);
	dataBuffer[4] = HIGH_BYTE(data);	
	dataBuffer[5] = CRC8(dataBuffer + 4, 1, CRC_KEY);		

	msg.addr = BQ769X0_I2C_ADDR;
	msg.flags = I2C_WR;	
	msg.buf = dataBuffer + 1;
	msg.tLen = 5;

	/* 写入16位寄存器（低字节+CRC、再高字节+CRC） */
	if (I2C_TransferMessages(&i2c1, &msg, 1) != 1)
	{
		LOG_E("Write Register Word With CRC Fail");
		return false;
	}

    return true;
}


static bool BQ769X0_WriteBlockWithCRC(uint8_t startAddress, uint8_t *buffer, uint8_t length)
{
	uint8_t index;
	uint8_t bufferCRC[32] = {0}, *pointer;
	struct I2C_MessageTypeDef msg = {0};

	pointer = bufferCRC;
	*pointer++ = BQ769X0_I2C_ADDR << 1;
	*pointer++ = startAddress;
	*pointer++ = *buffer;
	*pointer = CRC8(bufferCRC, 3, CRC_KEY);

	for(index = 1; index < length; index++)
	{
        pointer++;
        buffer++;
        *pointer = *buffer;
		*(pointer + 1) = CRC8(pointer, 1, CRC_KEY);
		pointer++;
	}

	msg.addr = BQ769X0_I2C_ADDR;
	msg.flags = I2C_WR;	
	msg.buf = bufferCRC + 1;
	msg.tLen = 2 * length + 1;

	/* 批量写入，每个数据字节后附带一个CRC字节 */
	if (I2C_TransferMessages(&i2c1, &msg, 1) != 1)
	{
		LOG_E("Write Register Block With CRC Fail");
		return false;
	}

    return true;	
}



static bool BQ769X0_ReadRegisterByte(uint8_t Register, uint8_t *data)
{  	
	struct I2C_MessageTypeDef msg[2] = {0};

	msg[0].addr = BQ769X0_I2C_ADDR;
	msg[0].flags = I2C_WR;	
	msg[0].buf = &Register;
	msg[0].tLen = 1;

	msg[1].addr = BQ769X0_I2C_ADDR;
	msg[1].flags = I2C_RD;	
	msg[1].buf = data;
	msg[1].tLen = 1;

	/* 先写寄存器地址，再读1字节数据（无CRC） */
	if (I2C_TransferMessages(&i2c1, msg, 2) != 2)
	{
		LOG_E("Read Register Byte Fail");
		return false;
	}

    return true;
}

static bool BQ769X0_ReadRegisterByteWithCRC(uint8_t Register, uint8_t *data)
{  	
	uint8_t readBuffer[2], crcInput[2], crcValue;
	struct I2C_MessageTypeDef msg[2] = {0};

	msg[0].addr = BQ769X0_I2C_ADDR;
	msg[0].flags = I2C_WR;	
	msg[0].buf = &Register;
	msg[0].tLen = 1;

	msg[1].addr = BQ769X0_I2C_ADDR;
	msg[1].flags = I2C_RD;	
	msg[1].buf = readBuffer;
	msg[1].tLen = 2;

	/* 读取一个字节及其CRC，先写寄存器地址再读两字节（data + crc） */
	if (I2C_TransferMessages(&i2c1, msg, 2) != 2)
	{
		LOG_E("Read Register Byte With CRC Fail");
		return false;
	}


	crcInput[0] = (BQ769X0_I2C_ADDR << 1) + 1;
	crcInput[1] = readBuffer[0];

	crcValue = CRC8(crcInput, 2, CRC_KEY);
	/* 验证CRC是否匹配，不匹配则读取失败 */
	if (crcValue != readBuffer[1])
	{
		LOG_E("Read Register Byte CRC Check Fail");
		return false;
	}

	*data = readBuffer[0];

	return true;
}



static bool BQ769X0_ReadRegisterWordWithCRC(uint8_t Register, uint16_t *data)
{  	
	uint8_t readBuffer[4], crcInput[2], crcValue;
	struct I2C_MessageTypeDef msg[2] = {0};

	msg[0].addr = BQ769X0_I2C_ADDR;
	msg[0].flags = I2C_WR;	
	msg[0].buf = &Register;
	msg[0].tLen = 1;

	msg[1].addr = BQ769X0_I2C_ADDR;
	msg[1].flags = I2C_RD;	
	msg[1].buf = readBuffer;
	msg[1].tLen = 4;

	/* 读取两个数据字节与对应的CRC字节（共4字节），逐项校验 */
	if (I2C_TransferMessages(&i2c1, msg, 2) != 2)
	{
		LOG_E("Read Register Word With CRC Fail");
		return false;
	}

	crcInput[0] = (BQ769X0_I2C_ADDR << 1) + 1;
	crcInput[1] = readBuffer[0];

	crcValue = CRC8(crcInput, 2, CRC_KEY);
	/* 校验第1字节后的CRC */
	if (crcValue != readBuffer[1])
	{
		LOG_E("Read Register Word CRC 1 Check Fail");
		return false;
	}

	crcValue = CRC8(readBuffer + 2, 1, CRC_KEY);
	/* 校验第2字节后的CRC */
	if (crcValue != readBuffer[3])
	{
		BQ769X0_ERROR("Read Register Word CRC 2 Check Fail");
		return false;
	}
	*data = (readBuffer[2] << 8) | readBuffer[0];

	return true;
}

static bool BQ769X0_ReadBlockWithCRC(uint8_t Register, uint8_t *buffer, uint8_t length)
{  	
	uint8_t index, crcValue, crcInput[2];
	uint8_t buf[32] = {0};
	uint8_t *readData = buf;
	struct I2C_MessageTypeDef msg[2] = {0};


	msg[0].addr = BQ769X0_I2C_ADDR;
	msg[0].flags = I2C_WR;	
	msg[0].buf = &Register;
	msg[0].tLen = 1;

	msg[1].addr = BQ769X0_I2C_ADDR;
	msg[1].flags = I2C_RD;	
	msg[1].buf = readData;
	msg[1].tLen = length * 2;

	/* 批量读：读取 length 个数据，每个数据后接1字节CRC，共 length*2 字节 */
	if (I2C_TransferMessages(&i2c1, msg, 2) != 2)
	{
		LOG_E("Read Register Block With CRC Cail");
		return false;
	}

	crcInput[0] = (BQ769X0_I2C_ADDR << 1) + 1;
	crcInput[1] = readData[0];

	crcValue = CRC8(crcInput, 2, CRC_KEY);
	readData++;
	/* 校验第一个字节的CRC并保存数据 */
	if (crcValue != *readData)
	{
		LOG_E("Read Register Block CRC 1 Check Fail");
		return false; 
	}
	else
	{
		*buffer = *(readData - 1);
	}

	for(index = 1; index < length; index++)
	{
		readData++;
		/* 依次校验后续每个字节的CRC并保存数据 */
		crcValue = CRC8(readData, 1, CRC_KEY);
		readData++;
		buffer++;

		if (crcValue != *readData)
		{
			LOG_E("Read Register Block CRC Check Fail");
			return false;        
		}
		else
		{
			*buffer = *(readData - 1);
		}
	}

	return true;
}


/******************************************************************************************/



/**************************************** 传感数据采集 *****************************************/

/* 更新单节电芯电压 250ms更新一次 */
void BQ769X0_UpdateCellVolt(void)
{
	uint8_t index = 0; // 电芯索引
	uint16_t iTemp = 0; // 原始ADC值（寄存器读出）
	uint8_t *pRawADCData = NULL;    
	uint32_t lTemp = 0;  // 中间换算值（mV）

	/* 读取连续的单体电压寄存器（带CRC），每个电芯占用2字节 */
	if (BQ769X0_ReadBlockWithCRC(VC1_HI_BYTE, &(Registers.VCell1.VCell1Byte.VC1_HI), BQ769X0_CELL_MAX << 1) != true)
	{
		// 读取失败，记录日志（调用者可决定如何处理）
		LOG_E("Update Cell Voltage Fail");
	}

	/* 指针指向寄存器缓存区，按两个字节为一个电芯电压解析 */
	pRawADCData = &Registers.VCell1.VCell1Byte.VC1_HI;
	for (index = 0; index < BQ769X0_CELL_MAX; index++)
	{
		/* 高低字节组合为ADC计数 */
		iTemp = (unsigned int)(*pRawADCData << 8) + *(pRawADCData + 1);
		/* 使用整数增益 iGain 将ADC计数转换为微伏再缩放到mV */
		lTemp = ((unsigned long)iTemp * iGain) / 1000;
		/* 加上ADC偏移（mV），这是芯片校准时的寄存器值 */
		lTemp += Adcoffset;
		/* 最终以V为单位保存到样本数据中 */
		BQ769X0_SampleData.CellVoltage[index] = lTemp / 1000.0;
		pRawADCData += 2; // 移动到下一个电芯的两个字节
	}
}


/* 热敏电阻温度 2s更新一次 */
void BQ769X0_UpdateTsTemp(void)
{
	uint8_t index;
	uint16_t iTemp = 0;
	float v_tsx = 0;
	float Rts = 0;
	uint8_t *pRawADCData = NULL;

	
	/* 如果当前不是热敏电阻采样模式，写寄存器切换到外部NTC模式并等待ADC稳定 */
	if (TempSampleMode != 0)
	{
		TempSampleMode = 0;
		if (BQ769X0_WriteRegisterByteWithCRC(SYS_CTRL1, 0x18) != true)
		{
			LOG_E("Update Tsx Temperature Fail");
		}
		BQ769X0_DELAY(2000); // 等待2秒让采样通道稳定
	}

	/* 读取热敏电阻通道的ADC数据（带CRC），每个通道两个字节 */
	if (BQ769X0_ReadBlockWithCRC(TS1_HI_BYTE, &(Registers.TS1.TS1Byte.TS1_HI), BQ769X0_TMEP_MAX << 1) != true)
	{
		LOG_E("Update Tsx Temperature Fail");
	}

	/* 解析每个通道的ADC值并将其转换为温度 */
	pRawADCData = &Registers.TS1.TS1Byte.TS1_HI;
	for(index = 0; index < BQ769X0_TMEP_MAX; index++, pRawADCData += 2)
	{
		/* 组合高低字节得到ADC原始计数 */
		iTemp = (uint16_t)(*pRawADCData << 8) | *(pRawADCData + 1);

		/* 文档中给出的系数将ADC计数转换为电压 (V) */
		v_tsx = iTemp * 0.000382;

		/* 将分压电路计算得到的热敏电阻阻值（Ω），假设上拉为10k */
		Rts = (10000 * v_tsx) / (3.3 - v_tsx);

		/* 根据阻值计算温度并保存 */
		BQ769X0_SampleData.TsxTemperature[index] = TempChange(Rts);
	}
}



/* 获取ic内部温度,2s更新一次,未测试好 */
void BQ769X0_UpdateDieTemp(void)
{
	uint16_t adc_value = 0;
	
	if (TempSampleMode != 1)
	{
		TempSampleMode = 1;
		if (BQ769X0_WriteRegisterByteWithCRC(SYS_CTRL1, 0x10) != true)
	 	{
			LOG_E("Update Die Temperature Fail");
	 	}
		BQ769X0_DELAY(2000);
	}

	if (BQ769X0_ReadRegisterWordWithCRC(TS1_HI_BYTE, &Registers.TS1.TS1Word) != true)
 	{
		LOG_E("Update Die Temperature Fail");
 	}
	/* 组合读取到的高低字节ADC值并转换为电压，再由手册给出的公式换算成温度（近似） */
	adc_value = (Registers.TS1.TS1Byte.TS1_HI << 8) | Registers.TS1.TS1Byte.TS1_LO;
	BQ769X0_SampleData.DieTemperature =  adc_value * 382.0 / 1000000.0; // 转换为V
	BQ769X0_SampleData.DieTemperature = 25 - ((BQ769X0_SampleData.DieTemperature - 1.2) / 0.0042);
}


/* 更新总电流 250ms更新一次 */
void BQ769X0_UpdateCurrent(void)
{
	int32_t temp;

	if (BQ769X0_ReadRegisterWordWithCRC(CC_HI_BYTE, &Registers.CC.CCWord) != true)
 	{
		LOG_E("Update Current Fail");
 	}
	/* 组合寄存器字并将其按手册系数转换为电流值（A） */
	temp = Registers.CC.CCByte.CC_HI << 8 | Registers.CC.CCByte.CC_LO;

	/* 如果16位值为负（2补码），转换为带符号值 */
	if(temp & 0x8000)
	{
		temp = -((~temp + 1) & 0xFFFF);
	}

	/* CC寄存器表示的是微伏量级：I = (CC_uV / Rsns)，将μV换算并除以Rsns得到电流（A） */
	BQ769X0_SampleData.BatteryCurrent = ((temp * 8.44) / RsnsValue ) * 0.000001;
}


/* 更新总电压 250ms更新一次 */
void BQ769X0_UpdateBatVolt(void)
{
	uint16_t adc_value;

	if (BQ769X0_ReadRegisterWordWithCRC(BAT_HI_BYTE, &Registers.VBat.VBatWord) != true)
 	{
		LOG_E("Update Battery Voltage Fail");
 	}
	/* 组合ADC原始计数并根据Gain与Offset计算总电压（V） */
	adc_value = Registers.VBat.VBatByte.BAT_HI << 8 | Registers.VBat.VBatByte.BAT_LO;
	BQ769X0_SampleData.BatteryVoltage = 4 * Gain * adc_value; // 乘以4为芯片测量拓扑系数
	BQ769X0_SampleData.BatteryVoltage += BQ769X0_CELL_MAX * Adcoffset; // 加上偏移量（mV）
	BQ769X0_SampleData.BatteryVoltage /= 1000; // mV -> V
}
/*********************************************************************************************/


// 报警处理
static void BQ769X0_AlertyHandler(void)
{
	uint8_t reg_value = 0, write_value = 0;

	BQ769X0_ReadRegisterByteWithCRC(SYS_STAT, &reg_value);
	if (reg_value & SYS_STAT_OCD_BIT)
	{
		/* 处理过流告警（OCD） */
		write_value |= SYS_STAT_OCD_BIT;
		if (AlertOps.ocd != NULL) AlertOps.ocd();
	}

	if (reg_value & SYS_STAT_SCD_BIT)
	{
		/* 处理短路告警（SCD） */
		write_value |= SYS_STAT_SCD_BIT;
		if (AlertOps.scd != NULL) AlertOps.scd();
	}

	if (reg_value & SYS_STAT_OV_BIT)
	{
		/* 处理过压告警（OV） */
		write_value |= SYS_STAT_OV_BIT;
		if (AlertOps.ov != NULL) AlertOps.ov();
	}	

	if (reg_value & SYS_STAT_UV_BIT)
	{
		/* 处理欠压告警（UV） */
		write_value |= SYS_STAT_UV_BIT;
		if (AlertOps.uv != NULL) AlertOps.uv();
	}

	if (reg_value & SYS_STAT_OVRD_BIT)
	{
		/* 处理OVRD（过压错误/锁定） */
		write_value |= SYS_STAT_OVRD_BIT;
		if (AlertOps.ovrd != NULL) AlertOps.ovrd();
	}	

	if (reg_value & SYS_STAT_DEVICE_BIT)
	{
		/* 处理设备错误标志 */
		write_value |= SYS_STAT_DEVICE_BIT;
		if (AlertOps.device != NULL) AlertOps.device();
	}
	
	if (reg_value & SYS_STAT_CC_BIT)
	{
		/* 处理CC（电流计数）相关警告 */
		write_value |= SYS_STAT_CC_BIT;
		if (AlertOps.cc != NULL) AlertOps.cc();
	}		

	/* 将已处理的告警位写回SYS_STAT以清除中断标志 */
	BQ769X0_WriteRegisterByteWithCRC(SYS_STAT, write_value);
}


// 获取增益和偏移量
void BQ769X0_GetADCGainOffset(void)
{
	BQ769X0_ReadRegisterByteWithCRC(ADCGAIN1, &(Registers.ADCGain1.ADCGain1Byte));
	BQ769X0_ReadRegisterByteWithCRC(ADCGAIN2, &(Registers.ADCGain2.ADCGain2Byte));
	BQ769X0_ReadRegisterByteWithCRC(ADCOFFSET, &(Registers.ADCOffset));
	
	/*GAIN is uV/LSB,OFFSET is mV*/
    /* 下面的位移是因为GAIN数据是由两个寄存器拼接而成的 */
	/* 将寄存器拼接为完整的增益值（uV/LSB），Gain 使用 mV/LSB 以便与其他mV单位计算统一 */
	Gain = (ADCGAIN_BASE + ((Registers.ADCGain1.ADCGain1Byte & 0x0C) << 1) + ((Registers.ADCGain2.ADCGain2Byte & 0xE0)>> 5)) / 1000.0;
	/* iGain 保存整数形式的增益（uV/LSB），用于整数运算避免浮点误差 */
	iGain = ADCGAIN_BASE + ((Registers.ADCGain1.ADCGain1Byte & 0x0C) << 1) + ((Registers.ADCGain2.ADCGain2Byte & 0xE0)>> 5);

	/* ADCOffset寄存器为8位补码表示的偏移（mV），这里转换为带符号的int8_t */
	if (Registers.ADCOffset <= 0x7F) 
	{
		Adcoffset = Registers.ADCOffset;    // 正数，直接赋值
	}
	else
	{
		Adcoffset = Registers.ADCOffset - 256;  // 负数，补码转换为负值
	}
	
	//BQ769X0_INFO("Adcoffset = %d, Registers.ADCOffset = %d", Adcoffset, Registers.ADCOffset);
}


// 配置寄存器
static void BQ769X0_Configuration(void)
{
	unsigned char ReadBuffer[8];

	/* 设置SYS_CTRL1：打开ADC并选择外部NTC作为温度来源 */
	Registers.SysCtrl1.SysCtrl1Byte = 0x18;

	/* 设置SYS_CTRL2：使能电流连续采样，默认关闭充放电MOS以保证安全 */
	Registers.SysCtrl2.SysCtrl2Byte = 0x40;

	/* 配置CC_CFG，手册建议初始化为0x19以获得更好性能 */
	Registers.CCCfg = 0x19;

	/* 将上面设置的一组寄存器写入并读回以进行校验 */
	BQ769X0_WriteBlockWithCRC(SYS_CTRL1, &(Registers.SysCtrl1.SysCtrl1Byte), 8);
	BQ769X0_ReadBlockWithCRC(SYS_CTRL1, ReadBuffer, 8);
	

//	BQ769X0_INFO("Read Compare SysCtrl1Byte 0x%02x 0x%02x", ReadBuffer[0]&0X7F, Registers.SysCtrl1.SysCtrl1Byte);
//	BQ769X0_INFO("Read Compare SysCtrl2Byte 0x%02x 0x%02x", ReadBuffer[1], Registers.SysCtrl2.SysCtrl2Byte);
//	BQ769X0_INFO("Read Compare Protect1Byte 0x%02x 0x%02x", ReadBuffer[2], Registers.Protect1.Protect1Byte);
//	BQ769X0_INFO("Read Compare Protect2Byte 0x%02x 0x%02x", ReadBuffer[3], Registers.Protect2.Protect2Byte);
//	BQ769X0_INFO("Read Compare Protect3Byte 0x%02x 0x%02x", ReadBuffer[4], Registers.Protect3.Protect3Byte);
//	BQ769X0_INFO("Read Compare OVTrip 0x%02x 0x%02x", ReadBuffer[5], Registers.OVTrip);
//	BQ769X0_INFO("Read Compare UVTrip 0x%02x 0x%02x", ReadBuffer[6], Registers.UVTrip);
//	BQ769X0_INFO("Read Compare CCCfg 0x%02x 0x%02x", ReadBuffer[7], Registers.CCCfg);
	
	
	/* 比较读回的寄存器值是否与期望相同（去掉ReadBuffer[0]的最高位以忽略负载检测位）
	 * 若不一致视为配置失败，打印错误并停机（需要人工复位）
	 */
	if( (ReadBuffer[0]&0X7F) != Registers.SysCtrl1.SysCtrl1Byte
	|| ReadBuffer[1] != Registers.SysCtrl2.SysCtrl2Byte
	|| ReadBuffer[2] != Registers.Protect1.Protect1Byte
	|| ReadBuffer[3] != Registers.Protect2.Protect2Byte
	|| ReadBuffer[4] != Registers.Protect3.Protect3Byte
	|| ReadBuffer[5] != Registers.OVTrip
	|| ReadBuffer[6] != Registers.UVTrip
	|| ReadBuffer[7] != Registers.CCCfg)
	{
		LOG_E("BQ769X0 config register fail,Please reset BMS board");
		while(1);
	}
}



// 检测是否接了负载
// 只有在没使能充电的情况下且CHG引脚电压大于0.7V才会检测到负载
bool BQ769X0_LoadDetect(void)
{
	/* 读取SYS_CTRL1寄存器并根据LOAD_PRESENT位判断是否有外部负载接入
	 * 仅当未处于充电状态时（CHG_ON==0）才进行负载检测
	 */
	BQ769X0_ReadRegisterWordWithCRC(SYS_CTRL1, (uint16_t *)&Registers.SysCtrl1.SysCtrl1Byte);
	if (Registers.SysCtrl2.SysCtrl2Bit.CHG_ON == 0) // 不在充电状态下
	{
		if (Registers.SysCtrl1.SysCtrl1Bit.LOAD_PRESENT)
		{
			return true; // 检测到负载
		}
	}
	return false; // 未检测到负载
}

// 唤醒BQ芯片
void BQ769X0_Wakeup(void)
{
	/* 通过在TS1引脚输出高电平脉冲来唤醒BQ芯片，然后切回输入以免影响采样
	 * 1s延时确保芯片完成启动序列
	 */
	BQ769X0_TS1_SetOutMode();
	GPIO_WriteBit(BQ769X0_TS1_GPIOx, BQ769X0_TS1_Pin, Bit_SET);
	BQ769X0_DELAY(1000);

	/* 拉低并切换为输入，避免对NTC或温度测量通道的干扰 */
	GPIO_WriteBit(BQ769X0_TS1_GPIOx, BQ769X0_TS1_Pin, Bit_RESET);
	BQ769X0_TS1_SetInMode();
	BQ769X0_DELAY(1000);
}

// 进入低功率模式
void BQ769X0_EntryShip(void)
{
	/* 按要求序列写入SYS_CTRL1的特定值以进入运输/低功耗模式 */
	BQ769X0_WriteRegisterByteWithCRC(SYS_CTRL1, 0x00);
	BQ769X0_WriteRegisterByteWithCRC(SYS_CTRL1, 0x01);
	BQ769X0_WriteRegisterByteWithCRC(SYS_CTRL1, 0x02);
}

// 控制充放电开关
void BQ769X0_ControlDSGOrCHG(BQ769X0_ControlTypedef ControlType, BQ769X0_StateTypedef NewState)
{
	if (NewState == BQ_STATE_ENABLE)
	{
		Registers.SysCtrl2.SysCtrl2Byte |= ControlType;
	}
	else
	{
		Registers.SysCtrl2.SysCtrl2Byte &= ~ControlType;
	}
	/* 将更新后的SysCtrl2写入芯片，使充放电MOS的控制位生效 */
	BQ769X0_WriteRegisterByteWithCRC(SYS_CTRL2, Registers.SysCtrl2.SysCtrl2Byte);
}


// 设置某个电芯均衡状态，可以位与多节，支持BQ769X0系列(相邻单元不能同时均衡)
void BQ769X0_CellBalanceControl(BQ769X0_CellIndexTypedef CellIndex, BQ769X0_StateTypedef NewState)
{
	static uint8_t CELL_BAL_VALUE[3] = {0};

	if (NewState == BQ_STATE_ENABLE)
	{
		CELL_BAL_VALUE[0] |= CellIndex & 0x1F;
		CELL_BAL_VALUE[1] |= (CellIndex >> 5) & 0x1F;
		CELL_BAL_VALUE[2] |= (CellIndex >> 10) & 0x1F;
	}
	else if (NewState == BQ_STATE_DISABLE)
	{
		CELL_BAL_VALUE[0] &= ~(CellIndex & 0x1F);
		CELL_BAL_VALUE[1] &= ~((CellIndex >> 5) & 0x1F);
		CELL_BAL_VALUE[2] &= ~((CellIndex >> 10) & 0x1F);
	}
	/* 将3字节均衡配置写回芯片：每位对应一个电芯的均衡开关状态 */
	BQ769X0_WriteBlockWithCRC(CELLBAL1, CELL_BAL_VALUE, 3);
}









/*
在电池管理系统（BMS）中，当检测到电压或电流异常时，
系统通常不会立刻切断电路，而是会等待一个短暂的“延时时间（Delay）”以过滤掉瞬间的干扰
*/

//放电短路保护延时
void BQ769X0_SCDDelaySet(BQ769X0_SCDDelayTypedef SCDDelay)
{
	Registers.Protect1.Protect1Bit.SCD_DELAY = SCDDelay;
	BQ769X0_WriteRegisterByteWithCRC(PROTECT1, Registers.Protect1.Protect1Bit.SCD_DELAY);
}

//放电过流保护延时
void BQ769X0_OCDDelaySet(BQ769X0_OCDDelayTypedef OCDDelay)
{
	Registers.Protect2.Protect2Bit.OCD_DELAY = OCDDelay;
	BQ769X0_WriteRegisterByteWithCRC(PROTECT2, Registers.Protect2.Protect2Bit.OCD_DELAY);
}

//欠压保护延时
void BQ769X0_UVDelaySet(BQ769X0_UVDelayTypedef UVDelay)
{
	Registers.Protect3.Protect3Bit.UV_DELAY = UVDelay;
	BQ769X0_WriteRegisterByteWithCRC(PROTECT3, Registers.Protect3.Protect3Bit.UV_DELAY);
}

//过压保护延时
void BQ769X0_OVDelaySet(BQ769X0_OVDelayTypedef OVDelay)
{
	Registers.Protect3.Protect3Bit.OV_DELAY = OVDelay;
	BQ769X0_WriteRegisterByteWithCRC(PROTECT3, Registers.Protect3.Protect3Bit.OV_DELAY);
}

//设置欠压保护阈值
void BQ769X0_UVPThresholdSet(uint16_t UVPThreshold)
{
	/* 将期望的欠压阈值（mV）转换为芯片寄存器编码并写入UV_TRIP */
	Registers.UVTrip = (uint8_t)((((uint16_t)((UVPThreshold - Adcoffset)/Gain/* + 0.5*/) - UV_THRESH_BASE) >> 4) & 0xFF);
	BQ769X0_WriteRegisterByteWithCRC(UV_TRIP, Registers.UVTrip);
}

//设置过压保护阈值
void BQ769X0_OVPThresholdSet(uint16_t OVPThreshold)
{
	/* 将期望的过压阈值（mV）转换为芯片寄存器编码并写入OV_TRIP */
	Registers.OVTrip = (uint8_t)((((uint16_t)((OVPThreshold - Adcoffset)/Gain/* + 0.5*/) - OV_THRESH_BASE) >> 4) & 0xFF);
	BQ769X0_WriteRegisterByteWithCRC(OV_TRIP, Registers.OVTrip);
}






// BQ芯片初始化
void BQ769X0_Initialize(BQ769X0_InitDataTypedef *InitData)
{
	/* 中断GPIO初始化 */
	BQ769X0_ALERT_Init();

	/* 进入运输/睡眠再唤醒：相当于软件复位BQ芯片 */
	BQ769X0_EntryShip();
	BQ769X0_DELAY(500);
	BQ769X0_Wakeup();

	/* 读取ADC校准参数（GAIN/OFFSET）用于后续电压电流换算 */
	BQ769X0_GetADCGainOffset();

	/* 保存用户传入的报警回调集合 */
	AlertOps = InitData->AlertOps;

	/* 设置保护相关寄存器：阈值/延时等 */
	/*
	对于 BQ769x0 系列电池监测芯片，
	硬件级别的保护功能（充电OV过压、放电UV欠压、放电SCD短路、放电OCD过流）
	在芯片正常工作状态下是 默认启用的，
	不需要额外再去设置 “Enable” 开关位。
	*/
	Registers.Protect1.Protect1Bit.SCD_THRESH = SCDThresh;//设置短路保护阈值
	Registers.Protect2.Protect2Bit.OCD_THRESH = OCDThresh;//设置过流保护阈值
	Registers.Protect1.Protect1Bit.SCD_DELAY  = InitData->ConfigData.SCDDelay;   //设置短路保护延时
	Registers.Protect2.Protect2Bit.OCD_DELAY  = InitData->ConfigData.OCDDelay;    //设置过流保护延时
	Registers.Protect3.Protect3Bit.UV_DELAY   = InitData->ConfigData.UVDelay;     //设置欠压保护延时
	Registers.Protect3.Protect3Bit.OV_DELAY   = InitData->ConfigData.OVDelay; 	  //设置过压保护延时

	/* 根据期望的OV/UV阈值（mV）计算芯片寄存器需要的编码值（参见芯片手册7.3.1.2.1） */
	Registers.OVTrip = (uint8_t)((((uint16_t)((InitData->ConfigData.OVPThreshold - Adcoffset)/Gain/* + 0.5*/) - OV_THRESH_BASE) >> 4) & 0xFF);
	Registers.UVTrip = (uint8_t)((((uint16_t)((InitData->ConfigData.UVPThreshold - Adcoffset)/Gain/* + 0.5*/) - UV_THRESH_BASE) >> 4) & 0xFF);

	/* 写入并校验寄存器配置 */
	BQ769X0_Configuration();

	LOG_I("BQ769X0 Initialize successful!");
}
