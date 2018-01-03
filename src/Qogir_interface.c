#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <cutils/properties.h>
#include <preprocess.h>
#include "DecideBackground.h"
#include "cfp_fsys.h"
#include "cfp_utils.h"
#include "Qogir_interface.h"
#include "Qogir_utils.h"

#include <android/log.h>
#define TAG       "cdfinger"
#define TIME_TAG       "cdfingerT"

#ifdef CDFINGER_LOG_ON
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,TAG,__VA_ARGS__)
#define LOGW(...)  __android_log_print(ANDROID_LOG_WARN,TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,TAG,__VA_ARGS__)
#define LOGT(...)  __android_log_print(ANDROID_LOG_DEBUG,TIME_TAG,__VA_ARGS__)

#else
#define LOGD(...)
#define LOGW(...)  __android_log_print(ANDROID_LOG_WARN,TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,TAG,__VA_ARGS__)
#define LOGT(...)  

#endif
volatile cfp_config_t gBaseConfig;
//volatile cfp_config_t gBaseConfig;
extern unsigned char gcmd_id;
static char gInterruptOKFlag = 0;

// factory
calibrationPara caliPara_factory = {0};
int32_t calType_factory = 0;

extern cfp_bgImg_t gBgImg;
extern uint8_t* gCaliParabuffer;
extern int chip_work_mode_switch;

#define SENSOR_SPEC_OFFSET          6
#define SENSOR_MEANVALUE_OFFSET     5

#define STABLE_IMAGE_TEST
//#undef STABLE_IMAGE_TEST
#ifdef STABLE_IMAGE_TEST
//uint32_t sImgIndex = 0;
uint32_t sStableImgCnt = 0;
#endif
int getStableFingerImage(int32_t fd ,uint8_t *imagebuf);
int getFingerImage(int32_t fd , uint8_t *imagebuf);
int getStableBgImage(int32_t fd, uint8_t *imagebuf, uint8_t agc);
int getCheckStableImage(int32_t fd, uint8_t *imagebuf, uint8_t agc);
int readConfigFile(cfp_config_t *config, cfp_bgImg_t *bgImg);
int write_cfp_config(cfp_adjust_t *config);
int write_str_to_config(char * str );
static long long last_time = 0;
static int last_nConvergenceFlag = 0;
const char * getVersion()
{
    return CFP_LIB_VERSION;
}

int cfp_creatIoctlVal(cdfinger_modify_reg_t *config, uint8_t reg, uint8_t offset, uint8_t cmd_val)
{
	if(config == NULL)	return -1;
	switch (reg)
	{
		case 0x21:
			config->reg21_modify_allow = 1;
		 	config->reg21_offset = offset;
			config->reg21_modval = cmd_val;
			break;
		case 0x22:
			config->reg22_modify_allow = 1;
		 	config->reg22_offset = offset;
			config->reg22_modval = cmd_val;
			break;		
		case 0x27:
			config->reg27_modify_allow = 1;
		 	config->reg27_offset = offset;
			config->reg27_modval = cmd_val;			
			break;
		default:
			memset(config, 0x00, sizeof(cdfinger_modify_reg_t));
			break;
	}

	return 0;
}

int cfp_modifyReg53(int32_t fd)
{
	int ret = 0;
	cdfinger_modify_reg_t cfg = {0};
	cfp_creatIoctlVal(&cfg, 0x27, 6, 0x53);
	ret = ioctl(fd, FPSDEV0_MODIFY_CMD, &cfg);

	return ret;
}

int cfp_modifyReg33(int32_t fd)
{
	int ret = 0;
	cdfinger_modify_reg_t cfg = {0};
	cfp_creatIoctlVal(&cfg, 0x27, 6, 0x33);
	ret = ioctl(fd, FPSDEV0_MODIFY_CMD, &cfg);
	
	return ret;
}

int cfp_calcMean(int fd, uint8_t agc, cfp_adjust_t *adjust_st)
{
	int ret ;
	ret = getStableBgImage(fd , adjust_st->bgImgBuf, agc);

	if(ret == DE_SUCCESS)
	{
		adjust_st->bgMeanVal = computeImgMeanVar(adjust_st->bgImgBuf, SENSOR_BUFFER_LENGTH); // count the image mean value
	}
	else
	{
		ret = -1;
        LOGE("Get gWgAgc Read_Slice_Image failed!");
	}

	return ret;
}

int calibrate_agc(int fd, cfp_adjust_t * adjust_st)
{
    uint32_t start, middle, ending;
	uint32_t meanTh;
	int cnts = 0;
	int ret = 0;

	start  = AGC_MIN;
	middle = (AGC_MAX + AGC_MIN) / 2;
	ending = AGC_MAX;
	meanTh = BEST_MEANS;

	LOGD("start adjust agc ... ");
	adjust_st->wgAgc = AGC_JUMP_H;
	if((ret = cfp_calcMean(fd, adjust_st->wgAgc, adjust_st)) < 0)
	{
		LOGE("Adjust agc: An error occurred! Adjust failed! ");
		return -2;
	}
	if (meanTh <= adjust_st->bgMeanVal) {
		start = AGC_JUMP_H;
	} else {
		ending = AGC_JUMP_H - 1;
	}

	LOGD("AGC adjust range: [%x - %x]", start, ending);
	while (ending >= start)
	{
		LOGD("curr adjust range: [%x - %x]", start, ending);
		cnts++;
		if (ending - start <= 1)
		{
			adjust_st->wgAgc = start;
			break;
		}

		adjust_st->wgAgc = (ending + start) / 2;

		if((ret = cfp_calcMean(fd, adjust_st->wgAgc, adjust_st)) < 0x00)  	return ret;

		LOGD("AGC adjust mean[%d]", adjust_st->bgMeanVal);

		if (meanTh == adjust_st->bgMeanVal)	break;

		if (meanTh < adjust_st->bgMeanVal)
			start = adjust_st->wgAgc;
		else if (meanTh > adjust_st->bgMeanVal)
			ending = adjust_st->wgAgc;
	}

	ret = cfp_calcMean(fd, adjust_st->wgAgc, adjust_st);

	LOGD("adjust result: wgAgc[%x], bgMeanVal[%d], iterator[%d]", adjust_st->wgAgc, adjust_st->bgMeanVal, cnts);

	return ret;
}

int cfp_decideBGImag(int fd, cfp_adjust_t * adjust_st)
{
	int ret = 0;
	
	// reg27[6] = 0x53
	LOGD("calibrate_agc, Reg27[6] = 0x53");	
	if((ret = cfp_modifyReg53(fd)) < 0)
	{
		LOGE("cfp_modifyReg53 failed[%d]", ret);
		return ret;
	}		
	if ((ret = calibrate_agc(fd, adjust_st)) < 0 )
	{
		LOGE("calibrate_agc failed:__LINE__");
		return ret;
	}
	int selectIdx;
	selectIdx = selectPar((uint16_t *)adjust_st->bgImgBuf, 0x53, SENSOR_PIXEL_SIZE);
	if (selectIdx == 1)
	{
		ret = 0;
		adjust_st->extData.reg27_53or33 = 0x53;
		LOGD("calibrate_agc success, Reg27[6] = 0x53");
	}
	else
	{
		// reg27[6] = 0x33
		LOGD("calibrate_agc, Reg27[6] = 0x33");
		if((ret = cfp_modifyReg33(fd)) < 0)
		{
			LOGE("cfp_modifyReg33 failed[%d]", ret);
			return ret;
		}
		if ((ret = calibrate_agc(fd, adjust_st)) < 0 )
		{
			LOGD("calibrate_agc failed:__LINE__");
			return ret;
		}	
		selectIdx = selectPar((uint16_t *)adjust_st->bgImgBuf, 0x33, SENSOR_PIXEL_SIZE);
		if (selectIdx == 1)
		{
			ret = 0;
			adjust_st->extData.reg27_53or33 = 0x33;
			LOGD("calibrate_agc success, Reg27[6] = 0x33");			
		}
		else
		{
			ret = -1;
			LOGE("calibrate_agc failed, neither Reg27[6] = 0x33 nor Reg27[6] = 0x53");
		}
	}

	LOGD("Exit cfp_decideBGImag");
	return ret;
}

int InitDevice(int forceInit)
{	
	LOGD(" %s \n", __func__);
	char filePath[128] = {0};
    
    int32_t m_fd = 0;
	int re = 0xff;
	if(forceInit == 1){
        m_fd = open(DEVICE_PATH, O_RDWR);
        if(m_fd < 0)    
        {
            LOGE("open %s failed. errno [%d]\n", DEVICE_PATH, errno);
            return -1;
        } 	
        ioctl(m_fd, FPSDEV0_INIT, 0xec);
        re = ioctl(m_fd, FPSDEV0_GETIMAGE, 1);
        LOGD("FPSDEV0_GETIMAGE = 0x%02x", re);
        close(m_fd);
        if(re != 0x0){
            LOGD("FPSDEV0_GETIMAGE = 0x%02x -------> return -1", re);
            return -1;
        }
    }
	
	// char val[2] = {0};
	cfp_adjust_t adjust_st = {0x00};
	int ret = 0;
	int fd = -1;
	uint16_t id = -1;
	int filesize = 0;
	int total_s = 0;
	
	total_s = sizeof(adjust_st.devID) + sizeof(adjust_st.triggerVal)
			+ sizeof(adjust_st.wgAgc) + sizeof(adjust_st.bgMeanVal) 
			+ sizeof(adjust_st.extData)
			+ SENSOR_BUFFER_LENGTH;
	if(forceInit == 0){
	    if(access(GetCalibrationFile(), F_OK) == 0)
	    {
			fd = cfp_file_open(GetCalibrationFile(), O_RDONLY);
		    cfp_file_read(fd, &id, sizeof(uint16_t));
			filesize = cfp_file_getLength(fd);
			cfp_file_close(fd);
			if(filesize == total_s)
			{
				if(id != DEVICE_ID)
				{
					LOGW("Dissonance between a device id and cdfinger file id!");
				}
			}else{
				LOGW("cdfinger length[%d] does not accord with a standard[%d]!", filesize, total_s);
			}
	    }
	}

	LOGD("In cdfinger file: device id[0x%02x]", id);

	// remove Calibration File
	cfp_file_remove(GetCalibrationFile());
	cfp_file_remove(GetCalibrationFile2());

	// remove test file
	sprintf(filePath, "%s/FingerImg", GetPrivateRoot2());
	cfp_path_clean(filePath);
	sprintf(filePath, "%s/checkint-not_finger", GetPrivateRoot2());
	cfp_path_clean(filePath);
	sprintf(filePath, "%s/checkint-is_finger", GetPrivateRoot2());
	cfp_path_clean(filePath);
	sprintf(filePath, "%s/auth", GetPrivateRoot2());
	cfp_path_clean(filePath);

    if(access(GetPrivateRoot(), F_OK) == -1)
    {
        cfp_file_mkdir(GetPrivateRoot());
        if(errno != 0)
        {
            LOGE(" mkdir %s failed, errno[%d]!", GetPrivateRoot(), errno);
        }
    }

    if(access(GetPrivateRoot2(), F_OK) == -1)
    {
        cfp_file_mkdir(GetPrivateRoot2());
        if(errno != 0)
        {
            LOGE(" mkdir %s failed, errno[%d]!", GetPrivateRoot2(), errno);
        }
    }

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0)
    {
		LOGE("open %s failed errno[%d]\n", DEVICE_PATH, errno);
		return -1;
    }
    LOGD("fd = %d\n", fd);

	if ( calibrate_agc(fd, &adjust_st) < 0 )
	{
    	char *str = "Agc adjust failed!";
    	write_str_to_config(str);
    	LOGE("%s", str);
		close(fd);
		return -1;
	}
	
//	adjust_st.triggerVal = ioctl(fd, FPSDEV0_ADJUST_INTERRUPT);
//    LOGD("gTriggerVal = %d", adjust_st.triggerVal);
//    if(adjust_st.triggerVal <= 0xA0)
//    {
//        char *str = "Interrupt trigger value adjust failed!";
//    	write_str_to_config(str);
//    	LOGE("%s", str);
//		close(fd);
//    	return -1;
//    }

	LOGD("For 0x%02x, triggerVal -= %d", DEVICE_ID, INTERRUPT_OFFSET);
	adjust_st.triggerVal -= INTERRUPT_OFFSET; // 
	int triVal = adjust_st.triggerVal + 12;
	adjust_st.triggerVal = (triVal > 0xFF)? 0xFF : triVal;
	adjust_st.devID = DEVICE_ID;
    ret = write_cfp_config(&adjust_st);

    close(fd);

    return ret;
}

DeviceError cfp_devInit(char flag,cfp_bgImg_t *bgImg, int forceInit)
{
    int ret     = 0;
	int timeout = 3;
	int cnts    = timeout;

	while(flag)
	{
        if( InitDevice(forceInit) == 0)	break;
		--timeout;

        if (timeout <= 0)
        {
            ret = -1;
			LOGE("InitDevice() failed! repeat count:%d ", cnts);
            return ret;
        }
	}
	memset(bgImg, 0, sizeof(cfp_bgImg_t));
    ret = readConfigFile((cfp_config_t*)&gBaseConfig, bgImg);
    if(ret < 0)
    {
        LOGE("readConfigFile failed!");
        return ret;
    }

	// confige reg27
/*    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0)
    {
		LOGE("open %s failed errno[%d], fun:%s", DEVICE_PATH, errno, __func__);
		return -1;
    }
    LOGD("configure Reg27[6] = 0x%02x", gBaseConfig.extData.reg27_53or33);
	if(gBaseConfig.extData.reg27_53or33 == 0x53)
		ret = cfp_modifyReg53(fd);
	if(gBaseConfig.extData.reg27_53or33 == 0x33)
		ret = cfp_modifyReg33(fd);
	close(fd);
	LOGD("configure Reg27[6], ret[%d]", ret);
*/
    return ret;
}

/*
 *
 */
DeviceError cfp_devDeinit()
{
    return DE_SUCCESS;
}

void cfp_devGetBgImage(cfp_bgImg_t *bgImg)
{
#if 1
    char fileName[128] = {0};
    sprintf(fileName,"%s/B", GetPrivateRoot2());
    mkdir(fileName, 0777);
    
    sprintf(fileName, "%s/bg.bmp", fileName);
    fp_SaveGrayBmpFile(fileName, bgImg->buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
	sprintf(fileName, "%s/B/bg.csv", GetPrivateRoot2());
	fp_SaveCsvFile(fileName, bgImg->buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
    LOGD("cfp_devGetBgImage: %s updated", fileName);
#endif

	if (gCaliParabuffer == NULL)
	{
        gCaliParabuffer =  (uint8_t*)malloc(CALIPARA_LEN);
		memset(gCaliParabuffer, 0x00, CALIPARA_LEN);
		LOGD( "malloc gCaliParabuffer success");
	}

	bgImg->calType = 0;
	
	LOGD("loadCalibrationParameter failed: preProcess");
	preProcess((uint16_t*)bgImg->buffer, SENSOR_BUFFER_LENGTH, &(bgImg->caliPara), IS_PIXEL_CANCEL | IS_FLOTING | IS_COATING, &(bgImg->calType), THR_SELECT_BMP);

	bgImg->calType = 1;
    bgImg->isready = 1;
	LOGD("bgImg calType %d", bgImg->calType);
}

DeviceError cfp_devReadStableFingerImage(uint8_t* image)
{
    int32_t fd = 0;
    DeviceError err = DE_SUCCESS;
    // LOGD("cfp_devReadStableFingerImage enter...");
    fd = open(DEVICE_PATH,O_RDWR);
    if (fd == -1) {
        err = DE_OPEN_FAILED;
    }
    else
    {
        err = getStableFingerImage(fd, image);
        if (err != DE_SUCCESS)
        {
            LOGE("getStableFingerImage failed! %d", err);
        }
    }
    close(fd);
    //LOGD("cfp_devReadStableFingerImage out with value 0x%02x", err);
    return err;
}

DeviceError cfp_devReadFingerImage(uint8_t* image)
{
    LOGD("Leosin--> common : Qogir_interface.c -> cfp_devReadFingerImage(uint8_t* image)");

    int32_t fd = 0;
    DeviceError err = DE_SUCCESS;
    //LOGD("cfp_devReadFingerImage enter...");
    fd = open(DEVICE_PATH,O_RDWR);
    if (fd == -1) {
        err = DE_OPEN_FAILED;
    }
    else
    {
        LOGW("gBgImg.calipara.nConvergenceFlag[%d]", gBgImg.caliPara.nConvergenceFlag);
        //LOGD("Leosin--> common : Qogir_interface.c -> cfp_devReadFingerImage -> gBgImg.calipara.nConvergenceFlag[%d]", gBgImg.caliPara.nConvergenceFlag);
        err = getFingerImage(fd, image);
        if (err != DE_SUCCESS)
        {
            if(err == DE_NOT_FINGER)
            {
                LOGD("getFingerImage failed! %d", err);
               // LOGD("Leosin--> common : Qogir_interface.c -> cfp_devReadFingerImage() getFingerImage failed! %d", err);
            }
            else
            {
                LOGE("getFingerImage failed! %d", err);
              //  LOGD("Leosin--> common : Qogir_interface.c -> cfp_devReadFingerImage() getFingerImage failed! %d", err);
            }
        }
    }
    close(fd);
    //LOGD("cfp_devReadImage out with value 0x%02x", err);
    return err;
}


DeviceError cfp_devReadUnStableImage(uint8_t* image, int allowEmpty)
{
    int32_t fd = 0;
    DeviceError err = DE_SUCCESS;
    GR_MOVE_FLAG ret = GR_NOT_FINGER;
    uint8_t buffer[SENSOR_BUFFER_LENGTH] = { 0x00 };
    uint8_t tmpMeanVal = 0;
    //LOGD("cfp_devReadImage enter...");
    fd = open(DEVICE_PATH,O_RDWR);
    if (fd == -1) {
        err = DE_OPEN_FAILED;
    }
    else
    {
        err = readOneFrame(fd, buffer, gBaseConfig.wgAgc, 10);
        if (err != DE_SUCCESS)
        {
            LOGE("cfp_devReadUnStableImage failed! %d", err);
        }
        else if(allowEmpty)
        {
            memcpy(image,buffer,SENSOR_BUFFER_LENGTH);
        }
        else if(!allowEmpty)
        {
			ret = isFingerDown((uint16_t*)buffer, &gBgImg.caliPara, &gBgImg.calType, CALI_NOTFINGERDOWNFRAMES_THRE);
            LOGD("isFingerDown return 0x%02x", err);
            memcpy(image,buffer,SENSOR_BUFFER_LENGTH);
        }
    }
    close(fd);
    //LOGD("cfp_devReadImage out with value 0x%02x", err);
    return err;
}

DeviceError cfp_devCheckFinger(int flag)
{
    int32_t fd = 0;
    DeviceError err = DE_SUCCESS;
    uint8_t buffer[SENSOR_BUFFER_LENGTH] = { 0x00 };
	GR_MOVE_FLAG rval = 0;	
    char dirName[128] = { 0 };
    char fileName[128] = { 0 };
	static uint32_t sImgIndex = 0;
	long long t0;
	
    if(IsNeedSaveImg()) {
        sprintf(dirName, "%s/FingerImg", GetPrivateRoot());
        mkdir(dirName, 0777);
		sprintf(fileName, "%s/fingerUpFail", dirName);
		mkdir(fileName, 0777);
		sprintf(fileName, "%s/fingerUpSuccess", dirName);
		mkdir(fileName, 0777);
    }

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd == -1) 
	{
        err = DE_OPEN_FAILED;
    }
    else
    {
    	if(flag == 0){
			err = getFingerImage(fd, buffer);
			//err =  cfp_devReadStableFingerImage(buffer);
			if (err != DE_SUCCESS)
			{
                if(err != DE_NOT_FINGER)
				    LOGE("cfp_devCheckFinger read image: failed(%d)", err);
				close(fd);
				return err;
			}
			close(fd);
    	}else{
    		err = readOneFrame(fd, buffer, gBaseConfig.wgAgc, 10);
			last_time = cfp_get_uptime();
			t0 = cfp_get_uptime();
    		rval = isFingerUp((uint16_t*)buffer, &gBgImg.caliPara, &gBgImg.calType);
			LOGT("isFingerUp return %d, Run_time(%d)ms", rval, (uint32_t)(cfp_get_uptime() - t0));
			if (rval == GR_NOT_FINGER) {
				if(IsNeedSaveImg())
				{
					sprintf(fileName, "%s/fingerUpSuccess/%05d.bmp", dirName, sStableImgCnt);
					fp_SaveGrayBmpFile(fileName, buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
					sStableImgCnt++;
				}
				err = DE_NOT_FINGER;
			} else {
				if(IsNeedSaveImg())
				{
					sprintf(fileName, "%s/fingerUpFail/%05d.bmp", dirName, sStableImgCnt);
					fp_SaveGrayBmpFile(fileName, buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
					sStableImgCnt++;
					//sprintf(fileName, "%s/fingerUpFail/%05d.csv", dirName, sImgIndex);
					//fp_SaveCsvFile(fileName, buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
				}
				err = DE_SUCCESS;
			}
			close(fd);
			LOGD("isFingerup return %d, err(0x%02x)", rval, err);
			return err;
		}
    }

   return err;
}

/*
* convert big-endian to little-endian for Bit16
*/
DeviceError convertBe2LeBit16(uint8_t *pRst, uint8_t *pSrc, int row, int col)
{
    LOGD("Leosin--> common : Qogir_interface.c -> convertBe2LeBit16()");

	int i;
	int len = row * col;

	if (pSrc == NULL || pRst == NULL)	return DE_BAD_PARAM;

	for ( i = 0 ; i < len; i++ )
	{
	    pRst[2*i]   = pSrc[2*i + 1];
		pRst[2*i+1] = pSrc[2*i];
	}

	return DE_SUCCESS;
}

/*
 * image transpose
 */
void transposeImageBit16(uint16_t *pMat, int w, int h, int bitperpixel)
{
	if(w * h > SENSOR_WIDTH * SENSOR_HEIGHT) return;
	
	uint16_t pTmp[SENSOR_WIDTH * SENSOR_HEIGHT] = {0};
	
	int i, j;
	for(i = 0; i < w; i ++)
	{
		for(j = 0; j < h; j++)
		{
			pTmp[j*w+i] = pMat[i*h+j];
		}
	}

	memcpy(pMat, pTmp, h*w);
}

/*
 * Read one image from device.
 */
DeviceError readOneFrame(int32_t fd, uint8_t *pimage, uint8_t argVal, int32_t timeout)
{
    LOGD("Leosin--> common : Qogir_interface.c -> readOneFrame(fd=%d,pimage=%d,argVal=%d,timeout=%d)",fd, *pimage, argVal, timeout);

    DeviceError err = 0;
    int ret = 0;
    if (pimage == NULL)
    {
        return DE_BAD_PARAM;
    }

    /*write agc to device*/
    long long int ts = 0;
    long long int tt = 0;
    int32_t ot = timeout;
	unsigned char tmp_img[SENSOR_BUFFER_LENGTH] = {0x00};
   
    tt = cfp_get_uptime();
    do
    {
        if (timeout == 0)
        {
            err = DE_IO_ERROR;
            break;
        }
        ts = cfp_get_uptime();
        LOGD("ioctl FPSDEV0_INIT 0x%02x to device", argVal);
       // LOGD("Leosin--> common : Qogir_interface.c -> readOneFrame() ioctl FPSDEV0_INIT 0x%02x to device", argVal);

        ret = ioctl(fd, FPSDEV0_INIT, argVal);
        if (ret != DE_SUCCESS)
        {
            LOGE("ioctl return failed value %d", ret);
           // LOGD("Leosin--> common : Qogir_interface.c -> readOneFrame() ioctl return failed value %d", ret);
            return DE_IO_ERROR;
        }
        LOGD("write agc 0x%02x to device spend %ld ms", argVal, (long int)(cfp_get_uptime()-ts));
        //LOGD("Leosin--> common : Qogir_interface.c -> readOneFrame() write agc 0x%02x to device spend %ld ms", argVal, (long int)(cfp_get_uptime()-ts));

        
        ts = cfp_get_uptime();
        ret = ioctl(fd, FPSDEV0_GETIMAGE, 1);
        LOGD("ioctl FPSDEV0_GETIMAGE spend %ld ms", (long int)(cfp_get_uptime()-ts));
       // LOGD("Leosin--> common : Qogir_interface.c -> readOneFrame() ioctl FPSDEV0_GETIMAGE spend %ld ms", (long int)(cfp_get_uptime()-ts));

        --timeout;
    } while (ret == -1);
    if(timeout == 0)
    {
        LOGE("ERROR: Read image timeout!");
      //  LOGE("Leosin--> common : Qogir_interface.c -> readOneFrame() ERROR: Read image timeout!");
		return err;
    }
    
    LOGD("ioctl count = %d", ot - timeout);
   // LOGD("Leosin--> common : Qogir_interface.c -> readOneFrame() ioctl count = %d", ot - timeout);

    ts = cfp_get_uptime();
	read(fd, tmp_img, SENSOR_BUFFER_LENGTH);
    LOGD("read pimage spend %ld ms", (long int)(cfp_get_uptime()-ts));
    LOGD("readOneFrame spend %ld ms", (long int)(cfp_get_uptime()-tt));

   // LOGD("Leosin--> common : Qogir_interface.c -> readOneFrame() read pimage spend %ld ms", (long int)(cfp_get_uptime()-ts));
    //LOGD("Leosin--> common : Qogir_interface.c -> readOneFrame() readOneFrame spend %ld ms", (long int)(cfp_get_uptime()-tt));


	convertBe2LeBit16(pimage, tmp_img, SENSOR_HEIGHT, SENSOR_WIDTH);
	
    return DE_SUCCESS;
}

int getStableFingerImage(int32_t fd , uint8_t *imagebuf)
{
	uint32_t sImgIndex = 0;
    char dirName[128] = { 0 };
    char fileName[128] = { 0 };
    if(IsNeedSaveImg()) {
        sprintf(dirName, "%s/stableFingerImg", GetPrivateRoot());
        mkdir(dirName, 0777);
    }
    static uint8_t image_buf_1[SENSOR_BUFFER_LENGTH] = { 0x00 };
    static uint8_t image_buf_2[SENSOR_BUFFER_LENGTH] = { 0x00 };
    int32_t score = 1000;
    DeviceError err = DE_SUCCESS;
    uint8_t stableImgcount = 0;
    uint32_t getImageCount = 0;
    uint8_t tmpSpec = 0;
    uint8_t tmpMeanVal = 0;
    uint8_t fingerUpCnt = 0;
    uint8_t useWhich = 0x2;
    
	int isFinger = 0;
	GR_MOVE_FLAG reval;
	
    LOGD("getStableFingerImage enter...");
    if(imagebuf == NULL)
    {
        LOGE("imagebuf is NULL");
        return DE_BAD_PARAM;
    }

    memset(image_buf_1,0x00, SENSOR_BUFFER_LENGTH);
    memset(image_buf_2,0x00, SENSOR_BUFFER_LENGTH);

    //while (getImageCount < 3)
    while (stableImgcount == 0)
    {
        if(IsNeedSaveImg()) {
            sprintf(fileName, "%s/%05d", dirName, sStableImgCnt);
            mkdir(fileName, 0777);
            sprintf(fileName, "%s/%05d-not_finger", dirName, sStableImgCnt);
            mkdir(fileName, 0777);
        }
        useWhich = (useWhich^0xFF) & 0x3;
        switch(useWhich)
        {
            case 1:
            {
                err = readOneFrame(fd, image_buf_1, gBaseConfig.wgAgc, 100);
                if (err != DE_SUCCESS)
                {
                    LOGE("readOneFrame failed after %d images", getImageCount);
                    return err;
                }
                ++getImageCount;
                if(IsNeedSaveImg()) {
                    sprintf(fileName, "%s/%05d/buf_1-%03d.bmp", dirName, sStableImgCnt, sImgIndex);
                    fp_SaveGrayBmpFile(fileName, image_buf_1, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
					sprintf(fileName, "%s/%05d/buf_1-%03d.csv", dirName, sStableImgCnt, sImgIndex);
					fp_SaveCsvFile(fileName, image_buf_1, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
                }
                break;
            }
            case 2:
            {
                err = readOneFrame(fd, image_buf_2, gBaseConfig.wgAgc, 100);
                if (err != DE_SUCCESS)
                {
                    LOGD("readOneFrame failed after %d images", getImageCount);
                    return err;
                }
                ++getImageCount;
                if(IsNeedSaveImg()) {
                    sprintf(fileName, "%s/%05d/buf_2-%03d.bmp", dirName, sStableImgCnt, sImgIndex);
                    fp_SaveGrayBmpFile(fileName, image_buf_2, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);					
					sprintf(fileName, "%s/%05d/buf_2-%03d.csv", dirName, sStableImgCnt, sImgIndex);
					fp_SaveCsvFile(fileName, image_buf_2, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
                }
				sImgIndex++;
                break;
            }
            default:
                break;
        }
        if(fingerUpCnt > 1)
        {
            LOGW("StableImage: Continous[%d] empty image!", fingerUpCnt);
            break;
        }
        if(getImageCount >= 2)
        {
            score = imageDvalue(image_buf_1,image_buf_2, SENSOR_BUFFER_LENGTH);
            if (score <= FPIMG_ABSDIFF_MAX)
            {
                stableImgcount++;
            }
			LOGD("image absdiff = %d, upper limit %d", score, FPIMG_ABSDIFF_MAX);
        }
    }
	
    if(stableImgcount != 0)
    {    	
		if(useWhich == 1)	memcpy(imagebuf, image_buf_1, SENSOR_BUFFER_LENGTH);
		if(useWhich == 2)	memcpy(imagebuf, image_buf_2, SENSOR_BUFFER_LENGTH);

		reval = isFingerDown((uint16_t*)imagebuf, &gBgImg.caliPara, &gBgImg.calType, CALI_NOTFINGERDOWNFRAMES_THRE);
		LOGD("isFingerDown return %d, %d", reval, GR_NOT_FINGER);
		if(reval == GR_NOT_FINGER)
		{
			err = DE_NOT_FINGER;
			if(IsNeedSaveImg()) {
				sprintf(fileName,"%s/%05d-not_finger/buf_2-%03d.bmp", dirName, sStableImgCnt, sImgIndex);
				fp_SaveGrayBmpFile(fileName, image_buf_2, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
			}			
		}else{
			err = DE_SUCCESS;
			if(IsNeedSaveImg()) {
	            sprintf(fileName, "%s/%05d/stableImg.bmp", dirName, sStableImgCnt);
	            fp_SaveGrayBmpFile(fileName, imagebuf, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
				sprintf(fileName, "%s/%05d/stableImg.csv", dirName, sStableImgCnt);
				fp_SaveCsvFile(fileName, imagebuf, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
				
	            LOGD("StableImage: Got stable image...after %d images, sImgIndex[%03d], sStableImgCnt[%03d]", getImageCount, sImgIndex, sStableImgCnt);
	        }
		}

        LOGD("StableImage: Got stable image...after %d images", getImageCount);
    }
    else
    {
		err = DE_FAILED;
		if(IsNeedSaveImg()) {
            LOGD("StableImage: Can not get stable image...after %d images, sImgIndex[%03d], sStableImgCnt[%03d]", getImageCount, sImgIndex, sStableImgCnt);
		}
        LOGW("StableImage: Can not get stable image...after %d images", getImageCount);
    }
    LOGD("getStableFingerImage exit");
    if(IsNeedSaveImg())
        sStableImgCnt++;
    return err;
}

#if 1
int getFingerImage(int32_t fd , uint8_t *imagebuf)
{
    LOGD("Leosin--> common : Qogir_interface.c -> getFingerImage(uint8_t* image)");

	uint32_t sImgIndex = 0;
    char dirName[128] = { 0 };
    char fileName[128] = { 0 };
	//static unsigned long long pretime=0;
	static int triggerDel = 0;
	unsigned long long currtime=0;
	char triger=0;
    if(IsNeedSaveImg()) {
        sprintf(dirName, "%s/checkint-is_finger", GetPrivateRoot());
        mkdir(dirName, 0777);
        sprintf(dirName, "%s/checkint-not_finger", GetPrivateRoot());
        mkdir(dirName, 0777);
        sprintf(dirName, "%s/FingerImg/notfinger", GetPrivateRoot());
        mkdir(dirName, 0777);
        sprintf(dirName, "%s/FingerImg/isfingerdown", GetPrivateRoot());
        mkdir(dirName, 0777);
		sprintf(dirName, "%s/FingerImg/FFTfingerdown", GetPrivateRoot());
        mkdir(dirName, 0777);
        sprintf(dirName, "%s/FingerImg", GetPrivateRoot());
        mkdir(dirName, 0777);
    }
    DeviceError err = DE_SUCCESS;
    uint32_t getImageCount = 0;
    
	GR_MOVE_FLAG reval=-1;
	long long t0, t1, t2;
	
    LOGD("getFingerImage enter...");
    //LOGD("Leosin--> common : Qogir_interface.c -> getFingerImage() getFingerImage enter...");

    if(imagebuf == NULL)
    {
        LOGE("imagebuf is NULL");
     //   LOGD("Leosin--> common : Qogir_interface.c -> getFingerImage() imagebuf is NULL");
        return DE_BAD_PARAM;
    }

    if (chip_work_mode_switch == 2) {  // KEY MODE NOT SET CHECK FLAG
		last_time = cfp_get_uptime();
	}

	if ((t2 = cfp_get_uptime() - last_time) >= 10 * 1000 && last_time != 0
			&& gBgImg.caliPara.nConvergenceFlag == 1) {
		if (gInterruptOKFlag == 0)
			gBaseConfig.triggerVal -= 0;
		gInterruptOKFlag = 1;
	}
    while (getImageCount < 1)
    {
    	if(gInterruptOKFlag == 1)
        	err = readOneFrame(fd, imagebuf, gBaseConfig.wgAgc, 100);
        else
   			err = getCheckStableImage(fd,imagebuf,gBaseConfig.wgAgc);
        if (err != DE_SUCCESS)
        {
        	triggerDel=0;
            LOGD("readOneFrame failed after %d images", getImageCount);
            return err;
        }
        ++getImageCount;
		
		if(gBgImg.caliPara.nConvergenceFlag  == 0) {
			LOGD("sunlin=====111111=");
				gBaseConfig.triggerVal=0xff;
				last_time = 0;
				gInterruptOKFlag=0;
		}
		LOGW("sunlin===nConvergenceFlag=%d===gInterruptOKFlag=%d",gBgImg.caliPara.nConvergenceFlag,gInterruptOKFlag);

		//(gBgImg.caliPara.nTimeout_ConvergenceFlag==1)
		if (gBgImg.caliPara.nConvergenceFlag > 0 && gInterruptOKFlag == 0) {
			LOGD("sunlin--22222222");
			t0 = cfp_get_uptime();
			reval = isCaliFingerDown((uint16_t*) imagebuf, &gBgImg.caliPara,&gBgImg.calType);
			last_time = cfp_get_uptime();
			LOGE("suns1====%llu", last_time);

			LOGT("isCaliFingerDown return %d, Run_time(%d)ms", reval, (uint32_t)(cfp_get_uptime() - t0));
			LOGD("sunlin=====reval=%d-----gInterruptOKFlag=%d",reval,gInterruptOKFlag);
			if (reval == GR_NOT_FINGER)
			{
				if (IsNeedSaveImg())
				{
					sprintf(fileName, "%s/checkint-not_finger/Img-%03d.bmp",
							GetPrivateRoot(), sStableImgCnt);
					fp_SaveGrayBmpFile(fileName, imagebuf, SENSOR_HEIGHT,
							SENSOR_WIDTH, SENSOR_BIT_WIDE);
				}
				if (!gInterruptOKFlag)
				{
					currtime = cfp_get_uptime();
					if((triggerDel%5==0)&&(triggerDel!=0)) {
						triggerDel = 0;
						gBaseConfig.triggerVal -= 2;
                        LOGW("0x%02x", gBaseConfig.triggerVal);
						last_time = cfp_get_uptime();
					}
					triggerDel ++;
					triger = gBaseConfig.triggerVal;
					LOGD("sunlin====triger=0x%x %d",triger, triggerDel-1);
				}
			}else{
				triggerDel = 0;
				if (IsNeedSaveImg()) {
					sprintf(fileName, "%s/checkint-is_finger/Img-%03d.bmp",
							GetPrivateRoot(), sStableImgCnt);
					fp_SaveGrayBmpFile(fileName, imagebuf, SENSOR_HEIGHT,
							SENSOR_WIDTH, SENSOR_BIT_WIDE);
				}
			}
		}

		t0 = cfp_get_uptime();
		reval = isFingerDown((uint16_t*)imagebuf, &gBgImg.caliPara, &gBgImg.calType, CALI_NOTFINGERDOWNFRAMES_THRE);
		LOGT("isFingerDown return %d, Run_time(%d)ms", reval, (uint32_t)(cfp_get_uptime() - t0));
		LOGD("nBKFFTdiffEnergy = %d", gBgImg.caliPara.nBkFFTdiffEnergy);

		if (last_nConvergenceFlag == 2 && gBgImg.caliPara.nConvergenceFlag == 1) {
			gBaseConfig.triggerVal = 0xff;
			gInterruptOKFlag = 0;
			last_time = 0;
		}
		last_nConvergenceFlag = gBgImg.caliPara.nConvergenceFlag;
		LOGD("sunlin===nConvergenceFlag=%d",gBgImg.caliPara.nConvergenceFlag);
		//if(gBgImg.caliPara.nBkCaliSUCCESSFlag == 1)
		//	isFingerUp((uint16_t*)imagebuf, &gBgImg.caliPara);
		t1 = cfp_get_uptime();	
        if(reval == GR_NOT_FINGER)
        {
            if(IsNeedSaveImg())
			{
                sprintf(fileName,"%s/notfinger/%05d.bmp", dirName, sStableImgCnt);
                fp_SaveGrayBmpFile(fileName, imagebuf, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
            }
			err = DE_NOT_FINGER;
			LOGD("Continous[%d] non-finger images!", getImageCount);
        }else if(reval == GR_IS_FFT_FINGER){
        	if(IsNeedSaveImg()) {
                sprintf(fileName, "%s/FFTfingerdown/%05d.bmp", dirName, sStableImgCnt);
                fp_SaveGrayBmpFile(fileName, imagebuf, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
            }
			err = DE_FFT_FINGER;
			LOGD("sunlin=== is FFT_FINGER");
        }else{
        	if(IsNeedSaveImg()) {
                sprintf(fileName, "%s/isfingerdown/%05d.bmp", dirName, sStableImgCnt);
                fp_SaveGrayBmpFile(fileName, imagebuf, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
            }
            triggerDel = 0;
			err = DE_SUCCESS;
        	//break;
        }
    }

    LOGD("getFingerImage exit");
    if(IsNeedSaveImg())
        sStableImgCnt++;
	
    return err;
}

#endif

int getStableBgImage(int32_t fd, uint8_t *imagebuf, uint8_t agc)
{
	uint32_t sImgIndex = 0;
    uint8_t image_buf_1[SENSOR_BUFFER_LENGTH] = { 0x00 };
    uint8_t image_buf_2[SENSOR_BUFFER_LENGTH] = { 0x00 };
    int32_t score = 1000;
    DeviceError err = DE_SUCCESS;
    uint8_t stableImgcount = 0;
    uint32_t getImageCount = 0;
    uint8_t tmpSpec = 0;
    uint8_t tmpMeanVal = 0;
    uint8_t useWhich = 0x2;
    char fileName[128] = { 0 };
	const uint32_t MaxCnts = 256;//max run time 256 * 40ms

    LOGD("getStableBgImage enter...");
    if(imagebuf == NULL)
    {
        LOGE("imagebuf is NULL");
        return DE_BAD_PARAM;
    }

    //while (getImageCount < 3)
    while (stableImgcount == 0)
    {
        useWhich = (useWhich^0xFF) & 0x3;
        switch(useWhich)
        {
            case 1:
            {
                err = readOneFrame(fd, image_buf_1, agc, 100);
                if (err != DE_SUCCESS)
                {
                    LOGE("readOneFrame failed after %d images", getImageCount);
                    return err;
                }
                ++getImageCount;
                break;
            }
            case 2:
            {
                err = readOneFrame(fd, image_buf_2, agc, 100);
                if (err != DE_SUCCESS)
                {
                    LOGE("readOneFrame failed after %d images", getImageCount);
                    return err;
                }
                ++getImageCount;
                break;
            }
            default:
                break;
        }

        if(getImageCount >= 2)
        {
            score = imageDvalue(image_buf_1,image_buf_2, SENSOR_BUFFER_LENGTH);
            if (score <= BGIMG_ABSDIFF_MAX)
            {
                stableImgcount++;
            }
			LOGD("getStableBgImage: score[%d], score upper limit %d", score, BGIMG_ABSDIFF_MAX);
        }

        if (getImageCount > MaxCnts) {
			break;
		}
    }
    if(stableImgcount != 0)
    {
        memcpy(imagebuf,image_buf_2,SENSOR_BUFFER_LENGTH);
    }
    else
    {
        err = DE_FAILED;
        LOGW("StableBgImage: Can not get stable image...after %d images", getImageCount);
    }
    LOGD("getStableBgImage exit");

    return err;
}
int getCheckStableImage(int32_t fd, uint8_t *imagebuf, uint8_t agc)
{
    LOGD("Leosin--> common : Qogir_interface.c -> getCheckStableImage(fd=%d, imagebuf=%d, agc=%d)",fd, *imagebuf, agc);

	uint32_t sImgIndex = 0;
    uint8_t image_buf_1[SENSOR_BUFFER_LENGTH] = { 0x00 };
    uint8_t image_buf_2[SENSOR_BUFFER_LENGTH] = { 0x00 };
    int32_t score = 1000;
    DeviceError err = DE_SUCCESS;
    uint8_t stableImgcount = 0;
    uint32_t getImageCount = 0;
    uint8_t tmpSpec = 0;
    uint8_t tmpMeanVal = 0;
    uint8_t useWhich = 0x2;
    char fileName[128] = { 0 };
	const uint32_t MaxCnts = 256;//max run time 256 * 40ms

    if(imagebuf == NULL)
    {
        LOGE("imagebuf is NULL");
        return DE_BAD_PARAM;
    }

    err = readOneFrame(fd, image_buf_1, agc, 100);
    if (err != DE_SUCCESS)
    {
        LOGE("readOneFrame failed after %d images", getImageCount);
       // LOGD("Leosin--> common : Qogir_interface.c -> getCheckStableImage() readOneFrame failed after %d images", getImageCount);
        return err;
    }
    ++getImageCount;
	
    err = readOneFrame(fd, image_buf_2, agc, 100);
    if (err != DE_SUCCESS)
    {
        LOGE("readOneFrame failed after %d images", getImageCount);
        //LOGD("Leosin--> common : Qogir_interface.c -> getCheckStableImage() readOneFrame failed after %d images", getImageCount);
        return err;
    }
    ++getImageCount;
	
    score = imageDvalue(image_buf_1, image_buf_2, SENSOR_BUFFER_LENGTH);
    LOGD("%s score = %d", __func__, score);
  //  LOGD("Leosin--> common : Qogir_interface.c -> getCheckStableImage() %s score = %d", __func__, score);

    if (score <= FPIMG_ABSDIFF_MAX)
    {
        memcpy(imagebuf, image_buf_1, SENSOR_BUFFER_LENGTH);
    }
    else
    {
        err = DE_NOT_FINGER;
        LOGD("StableBgImage: Can not get stable image...after %d images", getImageCount);
     //   LOGD("Leosin--> common : Qogir_interface.c -> getCheckStableImage() StableBgImage: Can not get stable image...after %d images", getImageCount);
    }

    return err;
}
int readConfigFile(cfp_config_t *config, cfp_bgImg_t *bgImg)
{
	int nbytes;
    FILE *fp = fopen(GetCalibrationFile(), "rb");
    if(fp == NULL)
    {    
		memset(bgImg->buffer + (SENSOR_HEIGHT/2 - 1)*SENSOR_LINE_LENGTH, 0x0f, SENSOR_LINE_LENGTH);
        LOGE("read %s open failed! %d", GetCalibrationFile(), errno);
        return -1;
    }

	fread(&config->devID,         sizeof(config->devID),            1, fp);
    fread(&config->triggerVal,    sizeof(config->triggerVal),       1, fp);
    fread(&config->wgAgc,         sizeof(config->wgAgc),            1, fp);
    fread(&config->bgMeanVal,     sizeof(config->bgMeanVal),        1, fp);
	fread(&config->extData,       sizeof(config->extData),          1, fp);
    nbytes = fread(bgImg->buffer, sizeof(uint8_t), SENSOR_BUFFER_LENGTH, fp);
    fclose(fp);

    if(nbytes != SENSOR_BUFFER_LENGTH)
    {
    	memset(bgImg->buffer + (SENSOR_HEIGHT/2 - 1)*SENSOR_LINE_LENGTH, 0x0f, SENSOR_LINE_LENGTH);
    }
    LOGD("read val_1 0x%04x, val_2 0x%02x, val_3 0x%02x, val_4 0x%04x, read image [%d] bytes",
    		config->devID, config->triggerVal, config->wgAgc, config->bgMeanVal, nbytes);

    return 0;
}

int write_cfp_config(cfp_adjust_t *config)
{
    FILE *fp = fopen(GetCalibrationFile(), "wb+");
    if(fp == NULL)
    {
    	LOGE("file %s open failed! errno [%d]", GetCalibrationFile(), errno);
    	return -1;
    }
	LOGD("writing 0x%02x & 0x%02x & 0x%04x to file %s", config->triggerVal, config->wgAgc, config->bgMeanVal, GetCalibrationFile());
    do
    {
        int nbyte = 0;
        int total;
		
		total = sizeof(config->devID) + sizeof(config->triggerVal)
			+ sizeof(config->wgAgc)	+ sizeof(config->bgMeanVal)
			+ sizeof(config->extData) + SENSOR_BUFFER_LENGTH;

		nbyte  = fwrite(&config->devID,       sizeof(uint8_t), sizeof(config->devID),        fp);
        nbyte += fwrite(&config->triggerVal,  sizeof(uint8_t), sizeof(config->triggerVal),   fp);
        nbyte += fwrite(&config->wgAgc,       sizeof(uint8_t), sizeof(config->wgAgc),        fp);
        nbyte += fwrite(&config->bgMeanVal,   sizeof(uint8_t), sizeof(config->bgMeanVal),    fp);
		nbyte += fwrite(&config->extData,     sizeof(uint8_t), sizeof(config->extData),      fp);
    	nbyte += fwrite(config->bgImgBuf,     sizeof(uint8_t), SENSOR_BUFFER_LENGTH,         fp);
    	int fd = fileno(fp);
        if(fd >= 0)
        {
    	    fchmod(fd, 0664);
        }
        fclose(fp);
        fp = NULL;
        LOGD("actually write %d bytes, expected write %d bytes!", nbyte, total);
        //if(nbyte == total)
       // {
        	//property_set("persist.cdfinger.ready", "1");
        //}else{
            //property_set("persist.cdfinger.ready", "0");
        //}
    }while(0);

    return 0;
}

int write_str_to_config(char * str )
{
	int ret = 0;
	uint8_t ImgTmp[SENSOR_BUFFER_LENGTH] = {0x00};
	char fillVal[14] = {0xFF, 0xFF, 0xFF, 0xFF};
    FILE *fp = fopen(GetCalibrationFile(), "wb+");
    if(fp == NULL)
    {
    	LOGE("file %s open failed! errno [%d]", GetCalibrationFile(), errno);
    	return -1;
    }

	LOGD("writing %s to file", str);
    fwrite(fillVal, sizeof(uint8_t), sizeof(fillVal), fp);

    memcpy(ImgTmp, str, strlen(str));
    memset(ImgTmp + (SENSOR_HEIGHT/2 - 1)*SENSOR_LINE_LENGTH, 0x0f, SENSOR_LINE_LENGTH);
    fwrite(ImgTmp, sizeof(uint8_t), SENSOR_BUFFER_LENGTH, fp);
	int fd = fileno(fp);
    if(fd >= 0)
    {
	    fchmod(fd, 0664);
    }
    fclose(fp);
    fp = NULL;

    //property_set("persist.cdfinger.ready", "0");

    return 0;
}

int loadCalibrationParameter(uint8_t *pCaliParabuffer)
{
	int reval = 0;
	int rBytes = 0;
	cfp_bgImg_t bgImg_temp;
	LOGD("loadCalibrationParameter -> -> ->");
    int fd = cfp_file_open(GetCalibrationFile2(), O_RDONLY);
    if(fd < 0)
    {
        LOGE("cfp_file_open %s failed![%d]", GetCalibrationFile2(), errno);
        return -1;
    }
    
    rBytes = cfp_file_read(fd, pCaliParabuffer, CALIPARA_LEN);
    cfp_file_close(fd);

	if(rBytes != CALIPARA_LEN)
	{
		LOGE("load CalibrationParameter failed, %d/%d", rBytes, CALIPARA_LEN);
		reval = -1;
	}else{
		LOGD("load CalibrationParameter success");
	}
	
    return reval;
}

int updateCalibrationParameter(uint8_t *pCaliParabuffer)
{
	int reval = 0;
	LOGD("updateCalibrationParameter -> -> ->");
    int fd = cfp_file_open(GetCalibrationFile2(), O_RDWR | O_CREAT);
    if(fd < 0)
    {
        LOGE("cfp_file_open %s failed![%d]", GetCalibrationFile2(), errno);
        return -1;
    }
    int wBytes = 0;
    wBytes = cfp_file_write(fd, pCaliParabuffer, CALIPARA_LEN);
    cfp_file_close(fd);

	LOGD("updateCalibrationParameter , %d/%d", wBytes, CALIPARA_LEN);
	if(wBytes != CALIPARA_LEN)
	{
		LOGE("update CalibrationParameter failed");
		reval = -1;
	}
	
    return reval;
}

int updateInterruptTriggerVal()
{
	if(gBgImg.caliPara.nConvergenceFlag  == 2) {
		gBaseConfig.triggerVal = ((gBaseConfig.triggerVal + 12) >= 0xFF)?0xFF:(gBaseConfig.triggerVal + 12);
	}
	LOGD("%s nConvergenceFlag=%d", __func__, gBgImg.caliPara.nConvergenceFlag);

	return 0;
}

int cfp_checkChipID()
{
	// check cdfinger id and chip-id
    if(access(GetCalibrationFile(), F_OK) == 0)
    {
        int fd;
        uint16_t id;
		fd = cfp_file_open(GetCalibrationFile(), O_RDONLY);
	    cfp_file_read(fd, &id, sizeof(uint16_t));
		cfp_file_close(fd);

		if(id != DEVICE_ID)
		{
			cfp_path_clean(GetPrivateRoot2());

			LOGW("Cdfinger-id[0x%02x], device-id[0x%02x]", id, DEVICE_ID);
			LOGW("Dissonance between a device id and cdfinger id!");
		}
    }

	return 0;
}

int factory_preprocess(void * pSrc, void * pRst, int isFirst)
{
	LOGD("enter ---->>>> %s", __func__);
	if(pSrc == NULL || pRst == NULL)	return -1;
	
	if(isFirst == 1)
	{
		calType_factory = 0;
		preProcess((uint16_t*)pSrc, SENSOR_BUFFER_LENGTH, &caliPara_factory, IS_PIXEL_CANCEL | IS_FLOTING | IS_COATING, &calType_factory, THR_SELECT_BMP);
		calType_factory = 1;
	}else{
		preProcess((uint16_t*)pSrc, SENSOR_BUFFER_LENGTH, &caliPara_factory, IS_PIXEL_CANCEL | IS_FLOTING | IS_COATING, &calType_factory, THR_SELECT_BMP);
		if (caliPara_factory.seleteIndex == 0)
			memcpy(pRst, caliPara_factory.dataBmp, SENSOR_BUFFER_LENGTH*sizeof(uint8_t));
		else
			memcpy(pRst, caliPara_factory.sitoBmp, SENSOR_BUFFER_LENGTH*sizeof(uint8_t));
	}
	LOGD("exit ---->>>> %s", __func__);

	return 0;
}

