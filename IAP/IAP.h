#ifndef __IAP_H__
#define __IAP_H__

#include "string.h"
#include "usart.h"
#include "crc16.h"

#pragma anon_unions
#pragma pack(1)

#define APP_BASE_ADDRESS ((uint32_t)(FLASH_BASE + 8 * 1024))
#define APP_END_ADDRESS ((uint32_t)(FLASH_BASE + 256 * 1024))

#ifndef APP_END_ADDRESS
#if defined(STM32F103xB)
#define APP_END_ADDRESS ((uint32_t)(FLASH_BASE + 128 * 1024))
#elif defined(STM32F103xE)
#define APP_END_ADDRESS ((uint32_t)(FLASH_BASE + 512 * 1024))
#elif defined(STM32F103xG)
#define APP_END_ADDRESS ((uint32_t)(FLASH_BASE + 1024 * 1024))
#elif defined(STM32F105xC)
#define APP_END_ADDRESS ((uint32_t)(FLASH_BASE + 256 * 1024))
#elif defined(STM32F107xC)
#define APP_END_ADDRESS ((uint32_t)(FLASH_BASE + 256 * 1024))
#endif
#endif

#define ERR_NUM_MAX 5

#define IAP_TIMEOUT 500
#define IAP_HEAD 1234

enum
{
    CMD_ACK = 0x00,
    CMD_START = 0x01,
    CMD_WRITE = 0x02,
    CMD_RESET = 0x03,
    CMD_UPDATE = 0x04,
};

typedef struct
{
    uint16_t usHead;    //帧头
    uint8_t ucCMD;      //命令
    uint16_t usDataLen; //数据长度，包括数据和检验
    uint8_t ucData;     //数据和检验值
    uint16_t usCRC;
} TXPacket_t;

typedef struct
{
    uint16_t usHead;          //帧头
    uint8_t ucCMD;            //命令
    uint16_t usDataLen;       //数据长度，包括数据和检验
    uint8_t ucData[1024 + 2]; //数据和检验值
} RXPacket_t;

#pragma pack()

#define RXEN_GPIO_Port GPIOA
#define RXEN_Pin GPIO_PIN_8

#define GPIO_WRITE_PIN(port, pin, val) ((val) != 0 ? (port->BSRR = pin) : (port->BRR = pin))

#define RXEN_ON() (GPIO_WRITE_PIN(RXEN_GPIO_Port, RXEN_Pin, 0))  //接收
#define RXEN_OFF() (GPIO_WRITE_PIN(RXEN_GPIO_Port, RXEN_Pin, 1)) //发送

extern uint8_t ucIAPInit(void);
extern uint8_t ucIAPProcess(void);

#endif
