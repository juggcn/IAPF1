#include "crc16.h"

uint16_t CRC16_Modbus(uint8_t *_pBuf, uint16_t _usLen)
{
	uint16_t i, j, temp, CRC;
	uint8_t CRCH, CRCL;
	CRC = 0xffff;
	for (i = 0; i < _usLen; i++)
	{
		CRC = _pBuf[i] ^ CRC;
		for (j = 0; j < 8; j++)
		{
			temp = CRC & 0x0001;
			CRC = CRC >> 1;
			if (temp)
				CRC = CRC ^ 0xA001;
		}
	}
	CRCL = CRC & 0xff;
	CRCH = (CRC >> 8) & 0xff;
	return CRC;
}