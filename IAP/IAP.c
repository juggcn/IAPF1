#include "IAP.h"
//串口相关的
static USART_TypeDef *huart = USART1;
static RXPacket_t xRXPacket;
static uint16_t usRXLen;
//APP相关的
typedef void (*pFunction)(void);
static pFunction JumpToApplication;
static uint32_t JumpAddress;
static void vIAPExecuteApp(void);
//大小端问题
static uint16_t BEBufToUint16(uint8_t *_pBuf)
{
    return (((uint16_t)_pBuf[0] << 8) | ((uint16_t)_pBuf[1] << 0));
}

static uint32_t BEBufToUint32(uint8_t *_pBuf)
{
    return (((uint32_t)_pBuf[0] << 24) | ((uint32_t)_pBuf[1] << 16) | ((uint32_t)_pBuf[2] << 8) | ((uint32_t)_pBuf[3] << 0));
}
//串口发送函数
static uint8_t ucSerialSend(uint8_t *pucDat, uint16_t usSize, uint32_t ulDelay)
{
    RXEN_OFF();
    HAL_Delay(1);
    uint8_t ucRes = 0;
    for (uint16_t i = 0; i < usSize; i++)
    {
        while (LL_USART_IsActiveFlag_TC(huart) == 0)
        {
            __NOP();
        }
        LL_USART_ClearFlag_TC(huart);
        LL_USART_TransmitData8(huart, *pucDat);
        pucDat++;
    }
    HAL_Delay(1);
    RXEN_ON();
    return ucRes;
}
//串口接受函数
static uint8_t ucSerialReceive(uint8_t *pucDat, uint16_t usSize, uint32_t ulDelay)
{
    uint8_t ucRes = 0;
    uint32_t ulTick = HAL_GetTick();
    while (LL_USART_IsActiveFlag_IDLE(huart) == RESET)
    {
        if ((ulTick + ulDelay) > HAL_GetTick())
        {
            if (LL_USART_IsActiveFlag_RXNE(huart))
            {
                LL_USART_ClearFlag_RXNE(huart);
                {
                    if (usRXLen < sizeof(RXPacket_t))
                    {
                        *(pucDat + usRXLen) = LL_USART_ReceiveData8(huart);
                        usRXLen++;
                    }
                    else
                    {
                        ucRes = 0; //正常
                        break;
                    }
                }
            }
        }
        else
        {
            ucRes = 1; //超时
            break;
        }
    }
    return ucRes; //正常
}

uint8_t ucIAPInit(void)
{
    uint8_t ucRes = 0;
    usRXLen = 0;
    HAL_GPIO_WritePin(RXEN_GPIO_Port, RXEN_Pin, GPIO_PIN_RESET);

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = RXEN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RXEN_GPIO_Port, &GPIO_InitStruct);

    memset(&xRXPacket, 0, sizeof(xRXPacket));
    HAL_Delay(100);
    return ucRes;
}
//发送应答包
static uint8_t ucIAPTransmitPacket(void)
{
    TXPacket_t xTXPacket;
    xTXPacket.usHead = IAP_HEAD;
    xTXPacket.ucCMD = CMD_ACK;
    xTXPacket.usDataLen = 0x01;
    xTXPacket.ucData = 0x00;
    /*大小端*/
    uint16_t usDat;
    usDat = xTXPacket.usHead;
    xTXPacket.usHead = BEBufToUint16((uint8_t *)&usDat);
    usDat = xTXPacket.usDataLen;
    xTXPacket.usDataLen = BEBufToUint16((uint8_t *)&usDat);
    /*计算数据校验*/
    xTXPacket.usCRC = CRC16_Modbus((uint8_t *)&xTXPacket, sizeof(TXPacket_t) - 2);
    /*数据发送*/
    return ucSerialSend((uint8_t *)&xTXPacket, sizeof(TXPacket_t), IAP_TIMEOUT);
}
//擦除flash
static uint8_t ucIAPFlashErasee(uint32_t addr, size_t size)
{
    uint8_t ucRes = 0;
    /* calculate pages */
    size_t erase_pages = size / FLASH_PAGE_SIZE;
    if (size % FLASH_PAGE_SIZE != 0)
    {
        erase_pages++;
    }
    /* start erase */
    HAL_FLASH_Unlock();
    uint32_t PageError = 0;
    FLASH_EraseInitTypeDef EraseInitStruct;
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.PageAddress = addr;
    EraseInitStruct.NbPages = erase_pages;
    if (HAL_FLASHEx_Erase(&EraseInitStruct, &PageError) != HAL_OK)
    {
        ucRes = 1;
    }
    HAL_FLASH_Lock();

    return ucRes;
}
//写flash
static uint8_t ucIAPFlashWrite(uint32_t addr, uint8_t *buf, size_t size)
{
    uint8_t ucRes = 0;
    HAL_FLASH_Unlock();
    for (size_t i = 0; i < size; i += 4, addr += 4)
    {
        /* write data */
        uint32_t ulDat = buf[i + 3] << 24 | buf[i + 2] << 16 | buf[i + 1] << 8 | buf[i];
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, ulDat);
        uint32_t read_data = *(uint32_t *)addr;
        /* check data */
        if (read_data != ulDat)
        {
            ucRes = 1;
            break;
        }
    }
    HAL_FLASH_Lock();
    return ucRes;
}
//IAP执行
uint8_t ucIAPProcess(void)
{
    HAL_StatusTypeDef status;
    uint32_t ulFlashDestination = 0;
    uint32_t ulUpdateCurSize = 0;
    uint32_t ulUpdateTolSize = 0;
    uint8_t ucErrNum = 0;
    while (1)
    {
        /************************接收数据***********************************/
        if (ucSerialReceive((uint8_t *)&xRXPacket, sizeof(RXPacket_t), IAP_TIMEOUT)) //如果超时
        {
            if (((*(__IO uint32_t *)APP_BASE_ADDRESS) & 0x2FFE0000) == 0x20000000) //只有当APP存在才运行进入APP
                goto exit;
        }
        /************************分析指令***********************************/
        if (LL_USART_IsActiveFlag_IDLE(huart) == SET)
        {
            LL_USART_ClearFlag_IDLE(huart);
            if (CRC16_Modbus((uint8_t *)&xRXPacket, usRXLen) == 0)
            {
                if (BEBufToUint16((uint8_t *)&xRXPacket.usHead) == IAP_HEAD)
                {
                    ucErrNum = 0;
                    switch (xRXPacket.ucCMD)
                    {
                        //上位机更新命令应答
                    case CMD_UPDATE:
                        memset(&xRXPacket, 0, sizeof(xRXPacket));
                        ucIAPTransmitPacket();
                        break;
                        //开始写指令
                    case CMD_START:
                        if (BEBufToUint16((uint8_t *)&xRXPacket.usDataLen) == 4)
                        {
                            ulFlashDestination = APP_BASE_ADDRESS;
                            ulUpdateCurSize = 0;
                            ulUpdateTolSize = BEBufToUint32(xRXPacket.ucData);
                            if (ulFlashDestination + ulUpdateTolSize <= APP_END_ADDRESS)
                            {
                                ucIAPTransmitPacket();
                                break;
                            }
                        }
                        goto exit;
                        //写指令
                    case CMD_WRITE:
                        if (ulFlashDestination == APP_BASE_ADDRESS && ulFlashDestination + ulUpdateTolSize <= APP_END_ADDRESS) //判断总容量是否在范围内
                        {
                            uint32_t ulAdd = ulFlashDestination + ulUpdateCurSize;
                            if (ulAdd <= APP_END_ADDRESS - FLASH_PAGE_SIZE) //判断擦除地址是否在范围内
                            {
                                if (ulAdd % FLASH_PAGE_SIZE == 0) //判断擦除地址是否为整页数
                                {
                                    if (ucIAPFlashErasee(ulAdd, FLASH_PAGE_SIZE) != 0) //擦除有问题就跳出来
                                    {
                                        break;
                                    }
                                }
                            }
                            uint32_t ulSize = BEBufToUint16((uint8_t *)&xRXPacket.usDataLen);
                            if (ulUpdateCurSize + ulSize > ulUpdateTolSize)
                            {
                                ulSize = ulUpdateTolSize - ulUpdateCurSize;
                            }
                            if (ucIAPFlashWrite(APP_BASE_ADDRESS + ulUpdateCurSize, (uint8_t *)xRXPacket.ucData, ulSize) == 0)
                            {
                                ulUpdateCurSize += ulSize;
                                ucIAPTransmitPacket();
                                break;
                            }
                        }
                        goto exit;
                        //启动APP
                    case CMD_RESET:
                        ucIAPTransmitPacket();
                        goto exit;

                    default:
                        goto exit;
                    }
                }
                else
                {
                    ucErrNum++; //错误次数累加
                }
            }
            else
            {
                ucErrNum++; //错误次数累加
            }
            usRXLen = 0;
        }
        if (ucErrNum >= ERR_NUM_MAX)
        {
            goto exit;
        }
    }
exit:
    HAL_Delay(100);
    __disable_irq(); //关闭全局中断
    vIAPExecuteApp();
}

static void vIAPExecuteApp(void)
{
    if (((*(__IO uint32_t *)APP_BASE_ADDRESS) & 0x2FFE0000) == 0x20000000)
    {
        /* Jump to user application */
        JumpAddress = *(__IO uint32_t *)(APP_BASE_ADDRESS + 4);
        JumpToApplication = (pFunction)JumpAddress;
        /* Initialize user application's Stack Pointer */
        __set_MSP(*(__IO uint32_t *)APP_BASE_ADDRESS);
        JumpToApplication();
    }
}