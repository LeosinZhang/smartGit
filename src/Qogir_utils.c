#include <stdint.h>
#include <stdlib.h>

#define IMAGE_EMPTY_THRESHOLD 2000


int32_t isImageEmpty(uint8_t *buffer , uint32_t bufLength)
{
    int32_t emptyCount = 0;
    int32_t i = 0;
    for (i = 0 ; i < bufLength ; i++)
    {
        if ( buffer[i] != 0xFF )
        {
            emptyCount++;
            if (emptyCount >= IMAGE_EMPTY_THRESHOLD)
            {
                return 0;
            }
        }
    }
    return -1;
}


int32_t imageDvalue(uint8_t* buffer1,uint8_t* buffer2,int32_t length)
{
    int32_t sum = 0;
    int32_t i = 0;
	int img_len = length/2;
	int x, y;

	if (length == 0)	return 0;
		
    for ( i = 0 ; i < img_len; i++ )
    {
    	x = buffer1[i*2] + (buffer1[i*2+1]&0x0F)*256;
		y = buffer2[i*2] + (buffer2[i*2+1]&0x0F)*256;
        sum += abs(x - y);
    }
	
    return (sum/img_len);
}

uint16_t computeImgMeanVar(uint8_t *pImage, uint32_t bufLength)
{
    uint32_t sum = 0;
    int i = 0;
	int cnts = bufLength/2;
	uint32_t imgsize = bufLength/2;
	
    for(i = 0; i < cnts; ++i)
    {
        sum += pImage[i*2] + ((pImage[i*2+1]&0x0f) << 8);
    }

    return (sum/imgsize);
}

uint8_t computeImgSpecValue(uint8_t *pImage, uint32_t bufLength)
{
    uint32_t sum = 0;
    uint8_t line_size = 112;
    uint8_t line_num = 3;
    uint8_t line_offset = 24;
    uint32_t pix_offset = 0;
    int line_cnt = 1;
    int j = 0;
    do {
    	pix_offset = (line_offset * line_cnt -1)*line_size;
        for(j = 0; j < line_size; ++j)
        {
            sum += pImage[pix_offset + j];
        }
        ++line_cnt;
    }while(line_cnt <= line_num);

    return (sum/(line_num*line_size));
}