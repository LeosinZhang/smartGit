#include "DecideBackground.h"

#define CORDIC_SQRT_SCALE_BITS		16
#define CORDIC_SQRT_SCALE_GAIN		65536 
#define CORDIC_SQRT_SCALE			39567 
#define CORDIC_SQRT_L				13	
#define SQRT_IN_GAIN_BITS			16	
#define SQRT_IN_GAIN				65536 

#define SHIFTBIT                 16
#define ShiftingR(val,nbits) ( ((val)<0)? (-((-(val))>>(nbits))):((val)>>(nbits)))

#define BP_GRAY_THRE_144 144 //坏点灰度阈值
#define BP_GRAY_THRE_185 185 //坏点灰度阈值

#define BPN_144 135 //坏点数==144的点的个数
#define BPN_185 290 //坏点数>185的点的个数

#define STD_THRE 35 //方差阈值


int64_t CorSqrt_local(int64_t xin)
{
	// Initial
	int64_t x, y, x_next, y_next, q_out;
	int32_t iter, k, presale_k = 0;

	//prescale;
	if (xin == 0)
	{
		return(0);
	}
	presale_k = 0;
	while (1)
	{
		if (xin<SQRT_IN_GAIN / 2)//0.5
		{
			xin = xin << 2;
			presale_k++;
		}
		else if (xin>12 * SQRT_IN_GAIN)//12
		{
			xin = xin >> 2;
			presale_k--;
		}
		else
		{
			break;
		}
	}

	x = xin + SQRT_IN_GAIN;//x=x+1
	y = xin - SQRT_IN_GAIN;//y=x-1

	k = 4; // Used for the repeated (3*k + 1) iteration steps
	// Iteration
	for (iter = 1; iter <= CORDIC_SQRT_L; iter++)
	{
		if (y < 0)
		{
			x_next = x + ShiftingR(y, iter);
			y_next = y + ShiftingR(x, iter);
		}
		else
		{
			x_next = x - ShiftingR(y, iter);
			y_next = y - ShiftingR(x, iter);
		}
		x = x_next;
		y = y_next;
		if (iter == k)
		{
			if (y < 0)
			{
				x_next = x + ShiftingR(y, iter);
				y_next = y + ShiftingR(x, iter);
			}
			else
			{
				x_next = x - ShiftingR(y, iter);
				y_next = y - ShiftingR(x, iter);
			}
			k = 3 * k + 1;
			x = x_next;
			y = y_next;
		}
	}
	q_out = x*CORDIC_SQRT_SCALE / CORDIC_SQRT_SCALE_GAIN;

	if (presale_k>0)
	{
		q_out = q_out >> presale_k;//q_out=q_out/(double)(2^presale_k);
	}
	else if (presale_k<0)
	{
		q_out = q_out << (-presale_k);//q_out=q_out*(double)(2^(-presale_k));
	}

	return(q_out);
}

//53参数调用这个
int32_t Static_53_badPoint(uint16_t* img, int32_t nPixelNum)
{
	int32_t ct144 = 0;
	int32_t ct185 = 0;
	int32_t nTemp = 0;
	int32_t i = 0;
	//144坏点和大于185像素点的计算
	for (i = 0; i < nPixelNum; i++)
	{
		nTemp = (int32_t)(*(img + i)) / SHIFTBIT;

		if (nTemp == BP_GRAY_THRE_144)
			ct144++;
		if (nTemp >= BP_GRAY_THRE_185)
			ct185++;
	}
	if (ct144 >= BPN_144 || ct185 >= BPN_185)
		return 0;
	else
		return 1;

}
//33参数调用这个
int32_t Static_33_badPoint(uint16_t* img, int32_t nPixelNum)
{
	int32_t ct144 = 0;
	int32_t nTemp = 0;
	int32_t i = 0;
	//144坏点
	for (i = 0; i < nPixelNum; i++)
	{
		nTemp = (int32_t)(*(img + i)) / SHIFTBIT;

		if (nTemp == BP_GRAY_THRE_144)
			ct144++;
	}
	if (ct144 >= BPN_144)
		return 0;
	else
		return 1;

}

//统计方差方差计算
int32_t Static_std(uint16_t*img, int32_t nPixelNum)
{
	int32_t i = 0;
	int32_t nMean = 0;
	int32_t nStd = 0;
	int32_t nTemp = 0;

	for (i = 0; i < nPixelNum; i++)
	{
		nTemp = (int32_t)(*(img + i)) / SHIFTBIT;
		nMean = nMean + nTemp;
	}

	nMean /= nPixelNum;

	for (i = 0; i < nPixelNum; i++)
	{
		nTemp = (int32_t)(*(img + i)) / SHIFTBIT;
		nStd = nStd + (nTemp - nMean)*(nTemp - nMean);
	}
	nStd /= nPixelNum;

	nStd = CorSqrt_local((int64_t)(nStd));

	nStd = (nStd >> 8);

	if (nStd >= STD_THRE)
		return 0;
	else
		return 1;
}

int32_t selectPar(uint16_t* img, int32_t par, int32_t nPixelNum)
{
	int32_t nisBP = 0;
	int32_t nisStd = 0;

	if (par == 0x33)
	{
		nisBP = Static_33_badPoint(img, nPixelNum);
		nisStd = Static_std(img, nPixelNum);

	}
	else
	{
		nisBP = Static_53_badPoint(img, nPixelNum);
		nisStd = Static_std(img,nPixelNum);
	}

	if (nisBP == 1 && nisStd == 1)
		return 1;
	else
		return 0;

}

