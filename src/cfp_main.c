#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cfpFingerprint.h"
#include "cfp_fingerprint.h"
#include "cfp_memory.h"
#include "cfp_fsys.h"
#include "cfp_utils.h"
#include "glimmerAl.h"
#include "cfpfile.h"
#include <sched.h>

#include "cfp_crc.h"
#include <cfp_log.h>
#include "glimmerAl.h"
#include "Qogir_interface.h"
#include "preprocess.h"
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

//#undef USE_MULTITHREAD_VERIFY
extern FeatureTable_t g_feature_table;
//static uint8_t *gBgImage = NULL;
static FeatureTable_t *pFeatureTable = NULL;
static uint32_t gRegisterCount = 0;
static uint32_t gRecognizeCount = 0;
static uint32_t gInvalidCount = 0;
static uint8_t gIsRegisterProcessing = 0;

#define SECURE_BUFF_LEN 511
//#define RESTRICT_DUPLICATE_FEATURE
static int gLoadFeatureSuccess = -1;

cfp_bgImg_t gBgImg = {0};
static volatile int gDetectRun = 0;
static int gCurrentUser = -1;
struct stat cfgStatus = {0};
unsigned char gcmd_id = -1;
int chip_work_mode_switch = 1; // low power  2 powerdown  0 idel
#ifdef CFP_ENV_ANDROID
#define CFP_IOC_MAGIC1 'G'
#define CFP_IOC_DISABLE_IRQ1 _IO(CFP_IOC_MAGIC1, 0)
#define CFP_IOC_ENABLE_IRQ1 _IO(CFP_IOC_MAGIC1, 1)
//extern int cfp_disable_irq(void);
//extern int cfp_enable_irq(void);
int g_tee_device_handle = 0;
static pthread_mutex_t gCfpTeeMutex = PTHREAD_MUTEX_INITIALIZER;
#elif defined(CFP_ENV_UNIX) || defined(CFP_ENV_WIN)
#elif defined(CFP_ENV_TEE_QSEE)
#endif

unsigned char SET_PATH[128] = {0};

extern int GetEnrollHighOverlayProgressSteps();
extern int GetEnrollOverlapThr();

typedef struct send_cmd_t
{
    unsigned char cmd_id;
    unsigned char data[MAX_CMDDATA_LEN];
} send_cmd;

typedef struct rsp_cmd_t
{
    unsigned char data[MAX_RSPDATA_LEN];
    unsigned char status;
} rsp_cmd;

typedef struct cmd0_data_t
{
    unsigned int ver;
    unsigned int age;
} cmd0_data;
typedef struct cmd0_rsp_t
{
    int featureID;
    char name[MAX_RECORD_NAME_LENGTH];
} cmd0_rsp;
typedef union rsp_data {
    int32_t ids[FEATURE_MAX_NUM];
    char name[FEATURE_NAME_LENGTH];
} rsp_data;
#define LSN_BUF_LEN (1408)
#define CMD_BUF_LEN (256)
typedef union SecPay_data {
    char secpay_id_name[LSN_BUF_LEN];   //num + x*(id+name)
    char secpay_recognize[CMD_BUF_LEN]; //len + emcryption(ret + id + time)
    char secpay_key[LSN_BUF_LEN];       //len + emcryption(is_exist + key)
} SecPay_data;
typedef struct cfp_cmd_data_t
{
    int32_t num;
    char *data;
} cfp_cmd_data;

typedef struct cfp_cmd_rsp_t
{
    int32_t num;
    rsp_data data;
    int32_t status;
    SecPay_data alipay_data;
} cfp_cmd_rsp;

typedef struct ali_sec_data_t
{
    int32_t available;
    unsigned int fpid; // template id
    uint32_t IDs[MAX_REGISTER_RECORD_NUM];
    unsigned int IDcount;
    int32_t match_state;
    unsigned long long time;
    unsigned int version;
    char hardware_id[HARDWARE_ID_LEN];
} ali_sec_data;

int recognizeByPolicy(uint8_t *buffer,
                      uint32_t *ids,
                      int32_t *whichOneTemplate,
                      int32_t *whichOnePatch,
                      int32_t *score,
                      int32_t *isupdate);

/** 
 * Sample app name 
 * Modify the app name to your specific app name  
 */
char TZ_APP_NAME[] = {"cdfinger_fp"};

//ialgorithm_t g_ialgorithm;
fp_register_record gFpRegisterRecords[MAX_REGISTER_RECORD_NUM];

REGISTER_STANDARD *gRegisterSession = NULL;
REGISTER_STANDARD *gRegisterSessionForBitmap = NULL;
Get_bitmap_info_t BitmapInfo = {0};
static int gRegisterMaxCnt = 0;
static int gUsedRegNum = REG_TIMES_MIN; //一个注册流程里注册进度次数

G_LEARN_Data *gLearnData = NULL;
glimmerImage gRecognSrc = {0};
GR_Template *gNewTemplate[MAX_REGISTER_RECORD_NUM] = {0};
int gCurrTemplatCnts = 0;
Recognize_Info_t gRecogResult;
uint8_t *gCaliParabuffer = NULL;

int LoadRegistTemps(int user_id)
{
    int i = 0;
    int j = 0;
    unsigned short crc = 0;
    char path[255] = {0};
    LOGD("LoadRegistTemps");
    //LOGD("Leosin-->common : cfp_main.c -> LoadRegistTemps(user_id = %d)", user_id);

    load_FeatureTable(user_id);
    pFeatureTable = get_FeatureTable(user_id);
    for (i = 0; i < MAX_REGISTER_RECORD_NUM; i++)
    {
        uint32_t tempsize = 0;
        uint8_t *pTempBuffer = NULL;
        memset(path, 0, 255);
        gFpRegisterRecords[i].status = FPALGO_FEATURE_EMPTY;
        if (pFeatureTable->FeatureTable_item[i].usedflag == 0)
        {
            LOGD("FeatureTable_item index = %d,not used\n", i);
            if (gFpRegisterRecords[i].pTemplate != NULL)
            {
                FreeTemplate(&(gFpRegisterRecords[i].pTemplate));
                gFpRegisterRecords[i].pTemplate = NULL;
            }
            continue;
        }
        for (j = 0; j < 2; j++)
        {
            LOGD("get_FeatureLength index %d", j);
            tempsize = get_FeatureLength(user_id,
                                         pFeatureTable->FeatureTable_item[i].FeatureID, j);

            LOGD("get_FeatureLength size= %d", tempsize);
            if (tempsize == 0 && j == 0)
            {
                LOGD("Feature %d is destoried...", pFeatureTable->FeatureTable_item[i].FeatureID);
                continue;
            }
            pTempBuffer = cfp_malloc(tempsize);
            if (pTempBuffer == NULL)
            {
                LOGE("qsee malloc fail %d.", __LINE__);
                return -1;
            }
            load_FeatureItem(user_id, pFeatureTable->FeatureTable_item[i].FeatureID, (char *)pTempBuffer, tempsize, j);

            if (j == 0)
                crc = pFeatureTable->FeatureTable_item[i].ItmeCrc16;
            if (j == 1)
                crc = pFeatureTable->FeatureTable_item[i].ItmeCrcBak16;

            if (crc == crc_ccitt((unsigned char *)pTempBuffer, tempsize))
            {
                LOGD("LoadRegistTemps,CRC PASS in item %d \n", i + 1);
                gFpRegisterRecords[i].status = FPALGO_FEATURE_USED;
                gFpRegisterRecords[i].feature_id = pFeatureTable->FeatureTable_item[i].FeatureID;

                if (gFpRegisterRecords[i].pTemplate != NULL)
                {
                    LOGD("LoadRegistTemps,FreeTemplate \n");
                    FreeTemplate(&(gFpRegisterRecords[i].pTemplate));
                    gFpRegisterRecords[i].pTemplate = NULL;
                }

                LOGD("LoadRegistTemps,IncreaseTemplateData \n");
                IncreaseTemplateData(pTempBuffer, tempsize, &(gFpRegisterRecords[i].pTemplate));

                //addElemt(i+1);
                cfp_free(pTempBuffer);
                break;
            }
            else if (j == 1)
            {
                //update table
                pFeatureTable->FeatureTable_item[i].usedflag = 0;
                if (0 != update_FeatureTable(user_id))
                {
                    LOGW("LoadRegistTemps,update_FeatureTable failed in item %d,remove it from table\n", i + 1);
                }
                LOGD("LoadRegistTemps,CRC failed in item %d,remove it form table\n", i + 1);
            }
            cfp_free(pTempBuffer);
        }
    }
    return 0;
}

int DeleteReferIndexRecord(int index)
{
    if ((index > 0) && (index <= MAX_REGISTER_RECORD_NUM))
    {

        if (gFpRegisterRecords[index - 1].status == FPALGO_FEATURE_USED)
        {
            //tempe_session = gFpRegisterRecords[index].register_session_item;
            //            templateDelete(gFpRegisterRecords[index - 1].pTemplate);
            gFpRegisterRecords[index - 1].feature_id = 0;

            if (gFpRegisterRecords[index - 1].pTemplate != NULL)
            {
                FreeTemplate(&gFpRegisterRecords[index - 1].pTemplate);
                gFpRegisterRecords[index - 1].pTemplate = NULL;
            }

            gFpRegisterRecords[index - 1].status = FPALGO_FEATURE_EMPTY;
            //then del file
            return 0;
        }
    }
    return -1;
}

int buffer_len = 512 * 1024;
void write_file_time_test(void)
{
    unsigned char *context_ptr[5] =
        {NULL};
    int fd = 0;
    int write_len = 0;
    int i = 0;

    fd = open("/data/tmpfile.dat", O_RDWR | O_CREAT | O_TRUNC, 0700);
    if (fd < 0)
    {
        LOGE("Failed to open file. fd = 0x%x\n", fd);
        return;
    }

    for (i = 0; i < 5; i++)
    {
        context_ptr[i] = cfp_malloc(buffer_len);
        if (context_ptr[i] == NULL)
        {
            LOGE("Failed to alloc memory. i = %d", i);
            close(fd);
        }
    }
    memset(context_ptr, 0x31, buffer_len);

    LOGD("up_time: %lld\n", (long long int)cfp_get_uptime());
    for (i = 0; i < 1; i++)
    {
        write_len = write(fd, context_ptr, buffer_len);
    }
    LOGD("up_time: %lld, wirte_len:0x%x\n", (long long int)cfp_get_uptime(), write_len);

    cfp_free(context_ptr);
    close(fd);
}

#ifdef CFP_ENV_TEE_QSEE
const char secure_path[] = "/persist/data/cdfinger_se128.txt";
void secure_file_test(void)
{
    //  fingerTempInfo* pTemplate;
    int fd = 0;
    int write_len = 0;
    int read_len = 0;
    uint32_t file_len = 0;
    char *sec_data_buf = NULL;
    long long t0, t1, t2, t3, t4, t5;
    int i;

    for (i = 1; i <= 2024; i *= 2)
    {
        int rm_rate = 0;
        int write_rate = 0;
        int read_rate = 0;
        int buf_len = i * 1024;

        t0 = cfp_get_uptime();
        qsee_sfs_rm(secure_path);
        t1 = cfp_get_uptime();
        fd = qsee_sfs_open(secure_path, O_RDWR | O_CREAT | O_TRUNC);
        if (fd == 0)
        {
            LOGE("Failed to open file. ret = %d\n", fd);
            return;
        }
        qsee_sfs_getSize(fd, &file_len);

        sec_data_buf = cfp_malloc(buf_len);
        memset(sec_data_buf, 0x31, buf_len);

        t2 = cfp_get_uptime();
        write_len = qsee_sfs_write(fd, sec_data_buf, buf_len);
        t3 = cfp_get_uptime();

        qsee_sfs_seek(fd, 0, SEEK_SET);
        t4 = cfp_get_uptime();
        read_len = qsee_sfs_read(fd, sec_data_buf, buf_len);
        t5 = cfp_get_uptime();

        cfp_free(sec_data_buf);
        if (t1 > t0)
        {
            rm_rate = (buf_len / (t1 - t0)) / 1024;
            file_len = file_len / 1024;
        }
        if (t3 > t2)
        {
            write_rate = (buf_len / (t3 - t2)) / 1024;
            write_len = write_len / 1024;
        }
        if (t5 > t4)
        {
            read_rate = (buf_len / (t5 - t4)) / 1024;
            read_len = read_len / 1024;
        }
        LOGD("rm[%d]:%dkb/ms, write[%d]:%dkb/ms, read[%d]:%dkb/ms.",
             file_len, rm_rate, write_len, write_rate, read_len, read_rate);
        qsee_sfs_close(fd);
    }
}
#endif

/**
 @brief
 Add any app specific initialization code here
 QSEE will call this function after secure app is loaded and
 authenticated
 */
/*
extern int fp_LoadB(const char* FilePath, unsigned char *pBitmap);
extern int fp_SaveGrayBitmap(const char* FilePath, unsigned char *pData,
        int row, int colume);
extern int fp_SaveRawData(const char* FilePath, unsigned short *pData, int row,
        int colume);
*/
void cfp_data_init(int userId)
{
    // check cdfinger id and chip-id
    if (access(GetCalibrationFile(), F_OK) == 0)
    {
        int fd;
        uint16_t id;
        fd = cfp_file_open(GetCalibrationFile(), O_RDONLY);
        cfp_file_read(fd, &id, sizeof(uint16_t));
        cfp_file_close(fd);

        if (id != DEVICE_ID)
        {
            cfp_file_remove(GetCalibrationFile());
            cfp_path_clean(GetPrivateRoot2());
            cfp_file_remove(GetCalibrationFile2());
            LOGW("Cdfinger-id[0x%02x], device-id[0x%02x]", id, DEVICE_ID);
            LOGW("Dissonance between a device id and cdfinger id!");
        }
    }

    //if (gLoadFeatureSuccess == -1)
    //if(access(GetPrivateRoot(), F_OK) != 0)
    {
        gLoadFeatureSuccess = init_fp_file(userId, "");
        if (!gLoadFeatureSuccess)
        {
            LoadRegistTemps(userId);
            LOGD("gLoadFeatureSuccess cfp_data_init!");
        }
        else
        {
            LOGW("init_fp_file failed! %d", gLoadFeatureSuccess);
        }
    }

    //load_TemplateIndexTable();
}

void cfp_reload_feature_table(int userId, int forceReload)
{
    LOGD("Leosin-->common : cfp_main.c -> cfp_reload_feature_table(userId = %d, forceReload = %d)", userId, forceReload);

    if ((gLoadFeatureSuccess == -1) || (forceReload == 1))
    {
        gLoadFeatureSuccess = init_fp_file(userId, "system");
        if (!gLoadFeatureSuccess)
        {
            LoadRegistTemps(userId);
            LOGD("gLoadFeatureSuccess tz_app_init!");
          //  LOGD("Leosin-->enroll : cfp_main.c -> cfp_reload_feature_table() gLoadFeatureSuccess tz_app_init!");
        }
        else
        {
            LOGE("init_fp_file failed!");
            //LOGD("Leosin-->enroll : cfp_main.c -> cfp_reload_feature_table() init_fp_file failed!");
        }
    }
}

/*
 *    @Description :
 *        Get index of space
 *    @Return:
 *        the index of space, -1 stand no space.
 */
int getRegisterIndex()
{
    LOGD("Leosin--> common : cfp_main.c -> getRegisterIndex()");

    int findIndex = -1;
    int i = 0;
    for (i = 0; i < MAX_REGISTER_RECORD_NUM; i++)
    {
        if (gFpRegisterRecords[i].status == FPALGO_FEATURE_REG_RESERVED)
        {
            findIndex = i;
            break;
        }
    }
    if (findIndex == -1)
    {
        for (i = 0; i < MAX_REGISTER_RECORD_NUM; i++)
        {
            if (gFpRegisterRecords[i].status == FPALGO_FEATURE_EMPTY)
            {
                findIndex = i;
                gFpRegisterRecords[i].status = FPALGO_FEATURE_REG_RESERVED;
                break;
            }
        }
    }
    LOGD("Leosin--> common : cfp_main.c -> getRegisterIndex() findIndex = %d", findIndex);
    return findIndex;
}

int32_t saveRegistTemp(int32_t *featureID, char *itemName)
{
    LOGD("%s, featureID = %d", __func__, *featureID);
    int TempSize = 0;
    // int write_len = 0;
    int i = 0;
    REGISTER_STANDARD *temp_session = NULL;
    GR_Template *pTemplate = NULL;
    uint8_t *pTempBuffer = NULL;
    for (i = 0; i < MAX_REGISTER_RECORD_NUM; i++)
    {
        if (gFpRegisterRecords[i].status == FPALGO_FEATURE_REG_RESERVED)
        {
            if (gRegisterSession != NULL)
            {
                registerGainTemplate(gRegisterSession, &pTemplate);
                TempSize = GainReduceTemplateDataedLen(pTemplate);
                pTempBuffer = (uint8_t *)cfp_malloc(TempSize);

                ReduceTemplateData(pTemplate, pTempBuffer);

                if (TempSize != save_FeatureItem(g_feature_table.FeatureTable_Header.userid,
                                                 i,
                                                 featureID,
                                                 itemName,
                                                 (char *)pTempBuffer, TempSize))
                {
                    cfp_free(pTempBuffer);
                    return -1;
                }

                if (gFpRegisterRecords[i].pTemplate != NULL)
                {
                    FreeTemplate(&(gFpRegisterRecords[i].pTemplate));
                    gFpRegisterRecords[i].pTemplate = NULL;
                }
                IncreaseTemplateData(pTempBuffer, TempSize, &(gFpRegisterRecords[i].pTemplate));
                if (pTempBuffer != NULL)
                    cfp_free(pTempBuffer);
                //qsee_sfs_close(fd);
                gFpRegisterRecords[i].status = FPALGO_FEATURE_USED;
                gFpRegisterRecords[i].feature_id = *featureID;
                gFpRegisterRecords[i].needUpdate = 0;
                EndRegister(&gRegisterSession, &pTemplate);
                gRegisterSession = NULL;
                return i + 1;
            }
            else
            {
                LOGE("why the RESERVED item,tempe_session==NULL!\n");
                //return 0;
                return -1;
            }
        }
    }
    // If we get here, we can not find a registered record
    // which status equals to FPALGO_FEATURE_REG_RESERVED
    // But we still need to end register session
    if (gRegisterSession != NULL)
    {
        GR_Template *pTemplate = NULL;
        registerGainTemplate(gRegisterSession, &pTemplate);
        EndRegister(&gRegisterSession, &pTemplate); // release session buffer
        gRegisterSession = NULL;
    }
    //return 0;
    return -1;
}

int32_t updateTemplate(int *featureID, int featureIndex, GR_Template *pTemplate)
{
    int tempSize = 0;
    uint8_t *pTempBuffer = NULL;

    tempSize = GainReduceTemplateDataedLen(pTemplate);
    pTempBuffer = (uint8_t *)cfp_malloc(tempSize);
    ReduceTemplateData(pTemplate, pTempBuffer);

    LOGD("gFpRegisterRecords[%p]", pTemplate);

    if (tempSize != update_FeatureItem(g_feature_table.FeatureTable_Header.userid,
                                       featureIndex, featureID,
                                       "cdfinger",
                                       (char *)pTempBuffer, tempSize))
    {
        cfp_free(pTempBuffer);
        return -1;
    }

    if (pTempBuffer != NULL)
        cfp_free(pTempBuffer);

    return 0;
}

#ifndef MIN
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#endif

void cfp_trustzone_cmd_handler(void *cmd, uint32_t cmdlen, void *rsp,
                               uint32_t rsplen)
{
    send_cmd *cmd_ptr = (send_cmd *)cmd;
    rsp_cmd *rsp_ptr = (rsp_cmd *)rsp;
    cmd0_data *cmd0_data_ptr = NULL;
    cmd0_rsp *cmd0_rsp_ptr = NULL;
    gcmd_id = cmd_ptr->cmd_id;
    unsigned char data = 0;
    //LOGD( "CFP_TZ: cmd = %d, cmd_len = %d, rsplen = %d\n ", cmd_ptr->cmd_id, cmdlen, rsplen);
    LOGD("Run on ChipID[0X%02x] cmd[%d]......................................", DEVICE_ID, cmd_ptr->cmd_id);
#ifdef CFP_ENV_ANDROID
    LOGD("gCfpTeeMutex lock--------------<<<<<<<<<<<");
    CreateRecognizeThreads();
    pthread_mutex_lock(&gCfpTeeMutex);
#elif defined(CFP_ENV_UNIX) || defined(CFP_ENV_WIN)
#elif defined(CFP_ENV_TEE_QSEE)

#endif

    switch (cmd_ptr->cmd_id)
    {
    case FP_REQUEST_INIT:
    {
        int32_t *pRev = (int32_t *)rsp;
        int forceInit = *((int *)cmd_ptr->data);
        LOGD("CFP_NZ: FP_REQUEST_INIT");
        cfp_checkChipID();

        stat(GetCalibrationFile(), &cfgStatus);
        if ((*pRev = cfp_devInit(1, &gBgImg, forceInit)) < 0)
        {
            break;
        }
        cfp_devGetBgImage(&gBgImg);
    }
    break;
    case FP_REQUEST_RESET:
        LOGD("CFP_NZ: FP_REQUEST_RESET");
        cmd0_data_ptr = (cmd0_data *)(cmd_ptr->data);
        cmd0_rsp_ptr = (cmd0_rsp *)(rsp_ptr->data);
        memcpy(cmd0_rsp_ptr->name, "cdfinger", 7);
        cmd0_rsp_ptr->featureID = 1;
        LOGD("CFP_NZ: ver:%d, age:%d\n", cmd0_data_ptr->ver, cmd0_data_ptr->age);
        LOGD("CFP_NZ: cmd0.name:%s, cmd0.featureID:%d\n", cmd0_rsp_ptr->name, cmd0_rsp_ptr->featureID);
        break;
    case FP_REQUEST_UPDATE_CALIBRATION:
        break;
    case FP_REQUEST_SETMODE:
    {
        LOGD("CFP_NZ: FP_REQUEST_SETMODE");
        int *ret = (int *)(rsp_ptr->data);
        uint32_t mode = *((uint32_t *)(cmd_ptr->data));
        if (mode == 0)
        {
            if (chip_work_mode_switch == 2)
                chip_work_mode_switch = 3; //turn down form low
            else
                chip_work_mode_switch = 1;
            if (*ret != 0)
            {
                LOGE("Set chip Interrupt Failed[%d]!", *ret);
            }
        }
        else if (mode == 1)
        {
            chip_work_mode_switch = 2;
            LOGD("CFP_NZ: FP_REQUEST_SETMODE KEY MODE %d\n", (*ret));
            if (*ret != 0)
            {
                LOGE("Set chip Interrupt Failed[%d]!", *ret);
            }
        }
        break;
    }
    case FP_REQUEST_GETMODE:
        break;

    case FP_REQUEST_REGIDTER:
    {
        //LOGD("Leosin-->enroll : cfp_main.c -> cfp_trustzone_cmd_handler() : FP_REQUEST_REGIDTER");
        int findIndex = -1;
        Register_Info_t *rsp_reg_info = (Register_Info_t *)rsp_ptr->data;
        long long t0, t1, t2, t3;
        char fileName[128] = {0};
        char dirName[128] = {0};
        static int reg_cnts = 0;
        LOGD("CFP_NZ: FP_REQUEST_REGIDTER %d \n", __LINE__);
        cfp_reload_feature_table(g_feature_table.FeatureTable_Header.userid, 0);
        if (gLoadFeatureSuccess == -1)
        {
            rsp_reg_info->register_status = REGISTER_INSUFFICIENT;
            rsp_reg_info->percent = (gUsedRegNum > 0) ? gUsedRegNum : 0;
            break;
        }
        findIndex = getRegisterIndex();
        if (findIndex == -1)
        {
            LOGW("no space for register");
          //  LOGD("Leosin-->enroll : cfp_main.c -> getRegisterIndex() no space for register");
            break;
        }

        if (NULL == gRegisterSession)
        {
           // LOGD("Leosin-->enroll : cfp_main.c -> getRegisterIndex()  NULL == gRegisterSession");

            reg_cnts++;
            gRegisterSession = beginRegister();

            gRegisterSession->nSchedule = 0;
            gRegisterSession->nUsedNum = 0;
            gUsedRegNum = GetEnrollSteps();
           // LOGD("Leosin-->enroll : cfp_main.c -> getRegisterIndex()  gUsedRegNum = %d", gUsedRegNum);

            if (gUsedRegNum < REG_TIMES_MIN)
                gUsedRegNum = REG_TIMES_MIN;
            if (gUsedRegNum > gRegisterSession->nMaxNum)
                gUsedRegNum = gRegisterSession->nMaxNum;
            LOGD("CFP_NZ: ( %p ,%d/%d),progress = %d", gRegisterSession, gRegisterSession->nUsedNum, gRegisterSession->nMaxNum, gRegisterSession->nSchedule);
            LOGD("CFP_NZ: current regtimes[%d], max regtimes[%d]", gUsedRegNum, gRegisterSession->nMaxNum);
           // LOGD("Leosin-->enroll : cfp_main.c -> cfp_trustzone_cmd_handler() : ( %p ,%d/%d),progress = %d", gRegisterSession, gRegisterSession->nUsedNum, gRegisterSession->nMaxNum, gRegisterSession->nSchedule);
           // LOGD("Leosin-->enroll : cfp_main.c -> cfp_trustzone_cmd_handler() : current regtimes[%d], max regtimes[%d]", gUsedRegNum, gRegisterSession->nMaxNum);
        }

        // get last remain steps
        rsp_reg_info->percent = (gUsedRegNum > 0) ? gUsedRegNum : 0;
       // LOGD("Leosin-->enroll : cfp_main.c -> getRegisterIndex()  rsp_reg_info->percent = %d", rsp_reg_info->percent);

        t0 = cfp_get_uptime();
        uint8_t buffer[SENSOR_BUFFER_LENGTH] = {0};
        //DeviceError de = cfp_devReadStableFingerImage(buffer);
       // LOGD("Leosin-->enroll : cfp_main.c -> cfp_devReadFingerImage(buffer) ");
        DeviceError de = cfp_devReadFingerImage(buffer);
        if (DE_SUCCESS != de)
        {
            LOGD("[FP_REQUEST_REGIDTER] : Get image failed[%d].", de);
          //  LOGD("Leosin-->enroll : cfp_main.c -> getRegisterIndex(): Get image failed de = [%d].", de);

            switch (de)
            {
            case DE_NOT_FINGER:
                rsp_reg_info->register_status = REGISTER_NOT_FINGER_IMAGE;
                break;

            case DE_FFT_FINGER:
                rsp_reg_info->register_status = REGISTER_DIRTY_IMAGE;
                break;

            default:
                rsp_reg_info->register_status = REGISTER_INSUFFICIENT;
                break;
            }

            break;
        }
        //LOGT("cfp_devReadStableFingerImage() spend %d ms", (uint32_t)(cfp_get_uptime()-t0));
        LOGT("cfp_devReadFingerImage() spend %d ms", (uint32_t)(cfp_get_uptime() - t0));
        //LOGT("Leosin-->enroll : cfp_main.c ->cfp_devReadFingerImage() spend %d ms", (uint32_t)(cfp_get_uptime() - t0));

        if (IsNeedSaveImg())
        {
            char subDir[128] = {0};
            sprintf(dirName, "%s/reg", GetPrivateRoot());
            mkdir(dirName, 0777);
            sprintf(subDir, "%s/original", dirName);
            mkdir(subDir, 0777);
            sprintf(fileName, "%s/Register-No%02d-%03d.bmp", subDir, reg_cnts, ++gRegisterCount);
            fp_SaveGrayBmpFile(fileName, buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
            sprintf(fileName, "%s/Register-No%02d-%03d.csv", subDir, reg_cnts, gRegisterCount);
            fp_SaveCsvFile(fileName, buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
        }

       // LOGD("Leosin-->enroll : cfp_main.c -> cfp_devReadFingerImage(buffer) --->> preProcess()");

        preProcess((uint16_t *)buffer, SENSOR_BUFFER_LENGTH, &gBgImg.caliPara, IS_PIXEL_CANCEL | IS_FLOTING | IS_COATING, &gBgImg.calType, THR_SELECT_BMP);
        glimmerQuaScore quality = {0};
        glimmerImage src = {0};
        src.layout.height = SENSOR_HEIGHT;
        src.layout.width = SENSOR_WIDTH;
        src.layout.bits = 8;
        src.ability = 1;
        src.layout.channels = 1;
        src.CT = 1;

        if (gBgImg.caliPara.frameNum1 <= SITO_COL_FRAMES)
            src.buffer = gBgImg.caliPara.sitoBmp;
        else
            src.buffer = gBgImg.caliPara.dataBmp;

        //src.buffer = gBgImg.caliPara.dataBmp;

        // TODO: reduce parameters of recognizeByPolicy
        int32_t whichOneTemplate = -1;
        int32_t whichOnePatch = -1;
        int32_t update = -1;
        int32_t score = 0;
        int32_t rst;
        uint32_t ids[MAX_REGISTER_RECORD_NUM] = {0};

        //if(gIsRegisterProcessing == 0)
        {
            rst = recognizeByPolicy(src.buffer, ids, &whichOneTemplate, &whichOnePatch, &score, &update);
            LOGD("all recognize thread exit!!!");
         //   LOGD("Leosin-->enroll : cfp_main.c -> cfp_devReadFingerImage() all recognize thread exit!!!");

            if (gcmd_id == FP_REQUEST_REGIDTER)
            {
                //do nothing
            }
            else
            {
            //    LOGD("Leosin-->enroll : cfp_main.c -> cfp_devReadFingerImage() --->> EndRecognition(&gLearnData)");
                EndRecognition(&gLearnData);
            }
            if (rst == 0)
            {
                // verify success shows that the finger has registered
                LOGW("[FP_REQUEST_REGIDTER] Registered finger again!");
                rsp_reg_info->register_status = REGISTER_DUPLICATE_FINGER;
                break;
            }
            gIsRegisterProcessing = 1;
        }

        int nOL = 0;
        int updata = 0;
        int32_t nNotMesFrms;
        int32_t overlapThre;

        nNotMesFrms = GetEnrollHighOverlayProgressSteps();
        overlapThre = GetEnrollOverlapThr();
        GR_UNUSUAL_U reg_res = registerImage(gRegisterSession, &src, &gBgImg.caliPara.diffBmp[0], &quality, &updata, &nOL, nNotMesFrms, overlapThre);
        switch (reg_res)
        {
        case GR_SUCCESS:
            gUsedRegNum--;
            rsp_reg_info->register_status = REGISTER_NORMAL;
            break;
        case GR_HIGH_OVERLAY:
            rsp_reg_info->register_status = REGISTER_DUPLICATE_AERA;
            break;
        case GR_LOW_QUALITY:
            rsp_reg_info->register_status = REGISTER_DIRTY_IMAGE;
            break;
        case GR_LOW_COVER:
            rsp_reg_info->register_status = REGISTER_LOW_COVER;
            break;
        default:
            rsp_reg_info->register_status = REGISTER_INSUFFICIENT;
            break;
        }
        rsp_reg_info->percent = (gUsedRegNum > 0) ? gUsedRegNum : 0;

        if (IsNeedSaveImg())
        {
            char subDir[128] = {0};
            sprintf(subDir, "%s/preprocessed", dirName);
            mkdir(subDir, 0777);
            sprintf(fileName, "%s/Reg-No%02d-%03d-Rst%02x.bmp", subDir, reg_cnts, gRegisterCount, reg_res);
            fp_SaveGrayBmpFile(fileName, src.buffer, SENSOR_HEIGHT, SENSOR_WIDTH, 8);
        }

        break;
    }
    case FP_REQUEST_SAVEREGISTER:
    {
        cfp_cmd_data *cmdData = 0;
        int32_t *ret = (int32_t *)rsp_ptr->data;
        long long t0, t1;
        cmdData = (cfp_cmd_data *)(cmd_ptr->data);

        LOGD("CFP_NZ: FP_REQUEST_SAVEREGISTER\n");
        t0 = cfp_get_uptime();
        ret[0] = saveRegistTemp(ret + 1, cmdData->data);
        if (ret[0] >= 0)
        {
            LOGD(/*"CFP_TZ: */ "Save Template succuss! ret = %d", *ret);
            saveCalibrationPara(&(gBgImg.caliPara), gCaliParabuffer);
            //updateCalibrationParameter(gCaliParabuffer);
            LOGD("FP_REQUEST_SAVEREGISTER: SaveCaliPara");
        }
        else
        {
            LOGE(/*"CFP_TZ: */ "Save Template failed!");
        }

        t1 = cfp_get_uptime();
        LOGD("CFP_NZ: Time[D]: save temp use:%lld ms\n", (t1 - t0));
        gIsRegisterProcessing = 0;
        break;
    }

    case FP_REQUEST_GET_FP_NAME:
    {
        int index = -1;
        LOGD("CFP_NZ: FP_REQUEST_GET_FP_NAME\n");
        index = *((int *)cmd_ptr->data);
        cmd0_rsp_ptr = (cmd0_rsp *)(rsp_ptr->data);
        memcpy(cmd0_rsp_ptr->name, "cdfinger", 8);
        cmd0_rsp_ptr->featureID = 0;
        if (index > 0 && index <= MAX_REGISTER_RECORD_NUM)
        {
            if (gFpRegisterRecords[index - 1].status == FPALGO_FEATURE_USED)
            {
                cmd0_rsp_ptr->featureID = gFpRegisterRecords[index - 1].feature_id;
                memcpy(cmd0_rsp_ptr->name, "cdfinger", 8);
            }
        }
        LOGD("CFP_NZ: index %d name:%s, cmd0.featureID:%d\n", *((int *)cmd_ptr->data), cmd0_rsp_ptr->name, cmd0_rsp_ptr->featureID);
        break;
    }
    case FP_REQUEST_CHANGE_FP_NAME:
        LOGD("CFP_TZ: FP_REQUEST_CHANGE_FP_NAME\n");
        cmd0_data_ptr = (cmd0_data *)(cmd_ptr->data);
        cmd0_rsp_ptr = (cmd0_rsp *)(rsp_ptr->data);
        memcpy(cmd0_rsp_ptr->name, "cdfinger", 7);
        cmd0_rsp_ptr->featureID = 1;
        LOGD("CFP_TZ: ver:%d, age:%d\n", cmd0_data_ptr->ver, cmd0_data_ptr->age);
        LOGD("CFP_TZ: cmd0.name:%s, cmd0.featureID:%d\n", cmd0_rsp_ptr->name, cmd0_rsp_ptr->featureID);
        break;
    case FP_REQUEST_RECOGNIZE:
    {
        cfp_reload_feature_table(g_feature_table.FeatureTable_Header.userid, 0);
        int i = 0;
        int loop = 1;
        cfp_cmd_recognize_rsp_data_t *pRsp = (cfp_cmd_recognize_rsp_data_t *)(rsp_ptr->data);
        pRsp->recognizeIndex = 0;
        pRsp->recognizeScore = 0;
        pRsp->recognizeID = 0;

        DeviceError de = DE_SUCCESS;
        uint8_t buffer[SENSOR_BUFFER_LENGTH] = {0};
        uint32_t ids[MAX_REGISTER_RECORD_NUM] = {0};
        char dirName[64] = {0};
        char subDir[64] = {0};
        char fileName[64] = {0};
        uint8_t *pSrc = NULL;
        long long t0, t1, t2;

        LOGD("CFP_TZ : FP_REQUEST_RECOGNIZE");

        t1 = cfp_get_uptime();
        de = cfp_devReadFingerImage(buffer);
        //de = cfp_devReadStableFingerImage(buffer);
        if (DE_SUCCESS != de)
        {
            LOGD("[FP_REQUEST_RECOGNIZE] : Get image failed[%d].", de);
            switch (de)
            {
            case DE_NOT_FINGER:
                pRsp->recognizeID = DE_NOT_FINGER;
                break;

            case DE_FFT_FINGER:
                pRsp->recognizeID = DE_FFT_FINGER;
                break;

            default:
                LOGE("[FP_REQUEST_RECOGNIZE] : Get image failed[0x%02x].", de);
                pRsp->recognizeID = DE_FAILED;
                break;
            }

            break;
        }
        LOGT("cfp_devReadFingerImage() spend %d ms", (uint32_t)(cfp_get_uptime() - t1));

        if (IsNeedSaveImg())
        {
            sprintf(dirName, "%s/auth", GetPrivateRoot());
            mkdir(dirName, 0777);
            sprintf(subDir, "%s/original", dirName);
            mkdir(subDir, 0777);
            sprintf(fileName, "%s/auth_original-%03d.bmp", subDir, ++gRecognizeCount);
            fp_SaveGrayBmpFile(fileName, buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
            sprintf(fileName, "%s/auth_original-%03d.csv", subDir, gRecognizeCount);
            fp_SaveCsvFile(fileName, buffer, SENSOR_HEIGHT, SENSOR_WIDTH, SENSOR_BIT_WIDE);
        }

        t1 = cfp_get_uptime();
        preProcess((uint16_t *)buffer, SENSOR_BUFFER_LENGTH, &gBgImg.caliPara, IS_PIXEL_CANCEL | IS_FLOTING | IS_COATING, &gBgImg.calType, THR_SELECT_BMP);

        if (gBgImg.caliPara.frameNum1 <= SITO_COL_FRAMES)
            pSrc = gBgImg.caliPara.sitoBmp;
        else
            pSrc = gBgImg.caliPara.dataBmp;
        t2 = cfp_get_uptime();
        LOGT("[CFP_HS]:preProcess() spend %d ms", (uint32_t)(t2 - t1));

        LOGD("Finger Down confirmed!");

        LOGD("[CFP_NZ] Begin Verfiy!");
        int32_t whichOneTemplate = -1;
        int32_t whichOnePatch = -1;
        int32_t update = -1;
        int32_t score = 0;
        t0 = cfp_get_uptime();
        recognizeByPolicy(pSrc, ids, &whichOneTemplate, &whichOnePatch, &score, &update);
        LOGT("[CFP_TZ] Verify Spend time: %d", (uint32_t)(cfp_get_uptime() - t0));
        LOGD("[CFP_TZ] Verfiy Done!\n");

        if (score > 0) // success
        {
            gRecogResult.whichOneTemplate = whichOneTemplate;
            gRecogResult.whichOnePatch = whichOnePatch;
            gRecogResult.score = score;
            gRecogResult.isUpdate = update;

            pRsp->recognizeIndex = whichOneTemplate + 1; //ids[whichOneTemplate];
            pRsp->recognizeScore = (score > 255) ? 255 : score;
            pRsp->recognizeID = gFpRegisterRecords[pRsp->recognizeIndex - 1].feature_id;

            LOGD("CFP_NZ: Index = %d , score = %d", pRsp->recognizeIndex, pRsp->recognizeScore);
            if (IsNeedSaveImg())
            {
                sprintf(subDir, "%s/success", dirName);
                mkdir(subDir, 0777);
                sprintf(fileName, "%s/auth_success-%03d-%d.bmp", subDir, gRecognizeCount, score);
                fp_SaveGrayBmpFile(fileName, pSrc, SENSOR_HEIGHT, SENSOR_WIDTH, 8);
            }
        }
        else
        { // failed
            pRsp->recognizeScore = -1;
            gRecogResult.whichOneTemplate = -1;
            gRecogResult.whichOnePatch = -1;
            gRecogResult.score = 0;
            gRecogResult.isUpdate = -1;
            EndRecognition(&gLearnData);

            if (IsNeedSaveImg())
            {
                sprintf(subDir, "%s/failed", dirName);
                mkdir(subDir, 0777);
                sprintf(fileName, "%s/auth_failed-%03d.bmp", subDir, gRecognizeCount);
                fp_SaveGrayBmpFile(fileName, pSrc, SENSOR_HEIGHT, SENSOR_WIDTH, 8);
            }
        }
        LOGD("CFP_TZ :FP_REQUEST_RECOGNIZE, recognizeIndex = %d", pRsp->recognizeIndex);
        break;
    }

    case FP_REQUEST_CANCEL_RECOGNIZE:
        LOGD("CFP_NZ: FP_REQUEST_CANCEL_RECOGNIZE\n");
        cmd0_data_ptr = (cmd0_data *)(cmd_ptr->data);
        cmd0_rsp_ptr = (cmd0_rsp *)(rsp_ptr->data);
        memcpy(cmd0_rsp_ptr->name, "cdfinger", 8);
        cmd0_rsp_ptr->featureID = 1;
        LOGD("CFP_NZ: ver:%d, age:%d\n", cmd0_data_ptr->ver, cmd0_data_ptr->age);
        LOGD("CFP_NZ: cmd0.name:%s, cmd0.featureID:%d\n", cmd0_rsp_ptr->name, cmd0_rsp_ptr->featureID);
        break;
    case FP_REQUEST_DEL_FP_TEMPLATES:
    {
        uint32_t count = 0;
        uint32_t *ids = NULL;
        uint32_t *pRst = (uint32_t *)cmd_ptr->data;
        uint32_t i = 0;
        int32_t *pRsp = (int32_t *)rsp_ptr->data;

        LOGD("CFP_NZ: FP_REQUEST_DEL_FP_TEMPLATES\n");

        count = pRst[0];
        ids = &(pRst[1]);

        *pRsp = 0;
        for (i = 0; i < count; i++)
        {
            int rm_ret = 0;
            rm_ret = remove_FeatureItem(g_feature_table.FeatureTable_Header.userid, ids[i] - 1);
            if (rm_ret != 0)
            {
                LOGE("CFP_TZ: Remove File Failed");
                //*pRsp = -1;
            }
            else
            {
                DeleteReferIndexRecord(ids[i]);
            }
        }
        break;
    }

    case FP_REQUEST_GET_FP_TEMPLATESLIST:
    {
        LOGD(" CFP_NZ: received FP_REQUEST_GET_FP_TEMPLATESLIST...");
        int32_t *template_list_rsp_ptr = (int32_t *)(rsp_ptr->data);
        int i = 0;
        int count = 0;
        cfp_reload_feature_table(g_feature_table.FeatureTable_Header.userid, 0);

        LOGD("CFP_NZ: FP_REQUEST_GET_FP_TEMPLATESLIST\n");

        *template_list_rsp_ptr = 0;
        for (; i < MAX_REGISTER_RECORD_NUM; i++)
        {
            if (gFpRegisterRecords[i].status == FPALGO_FEATURE_USED)
            {
                template_list_rsp_ptr[count + 1] = i + 1;
                count++;
            }
        }
        template_list_rsp_ptr[0] = count;
    }
    break;
    case FP_REQUEST_CANCEL_REGIDTER:
        LOGD("CFP_NZ: FP_REQUEST_CANCEL_REGIDTER\n");
        if (NULL != gRegisterSession)
        {
            GR_Template *pTemplate = NULL;
            registerGainTemplate(gRegisterSession, &pTemplate);
            EndRegister(&gRegisterSession, &pTemplate); // release session buffer
            gRegisterSession = NULL;
            gUsedRegNum = GetEnrollSteps();
        }
        gIsRegisterProcessing = 0;
        break;
    case FP_REQUEST_GET_BITMAP:
        break;
    case FP_REQUEST_REG_FROM_BMP:
        break;
    case FP_REQUEST_REG_FROM_BMP_CANCEL:
        break;
    case FP_REQUEST_REG_SAVE:
        break;
    case FP_REQUEST_RECOGNIZE_BMP:
        break;
    case FP_REQUEST_GET_HARDWARE_INFO:

        break;
    case FP_REQUEST_MP_TEST:

        break;
    case FP_REQUEST_DEL_BITMAP_TEMPLATES:
    {
        char lable[255] = {0};
        int index_id;
        LOGD("CFP_NZ: FP_REQUEST_DEL_BITMAP_TEMPLATES\n");
        sprintf(lable, "%s", cmd_ptr->data);
        LOGD("CFP_NZ:lable = %s.", lable);
        index_id = del_BitmapTemplate(lable);
        if (index_id == 0)
        {
            LOGE("Delete bitmap template fail.");
        }
    }
    break;
    case FP_REQUEST_GET_SHARE_DATA:
        break;
    case FP_REQUEST_GET_VERSION:
    {
        Version_Info_t *pRsp = (Version_Info_t *)(rsp_ptr->data);
        memset(pRsp, 0, sizeof(Version_Info_t));
        sprintf(pRsp->version_ta, "%x.%x.%x %s %s", CFP_APP_VERSION_MAJOR,
                CFP_APP_VERSION_MINOR, CFP_APP_VERSION_REV_VER, __DATE__,
                __TIME__);
        sprintf(pRsp->version_firmware, "%s", CFP_LIB_VERSION);
        sprintf(pRsp->version_algorithm, "%s", ALGO_VERSION);
    }
    break;
    case FP_REQUEST_SET_REGISTER_CNT:
    {
        int *pCount = (int *)cmd_ptr->data;

        if (*pCount > 30)
        {
            gRegisterMaxCnt = 30;
        }
        else if (*pCount < 5)
        {
            gRegisterMaxCnt = 5;
        }
        else
        {
            gRegisterMaxCnt = *pCount;
        }
        LOGD("CFP_NZ: FP_REQUEST_SET_REGISTER_CNT cnt = %d\n", gRegisterMaxCnt);
    }
    break;
    case FP_REQUEST_GET_REGISTER_CNT:
    {
        int *pRsp = (int *)(rsp_ptr->data);

        *pRsp = gRegisterMaxCnt;
        LOGD("CFP_NZ: FP_REQUEST_GET_REGISTER_CNT cnt = %d\n", gRegisterMaxCnt);
    }
    break;
    case FP_REQUEST_CHANGE_FP_PWD:
        LOGD("CFP_NZ: FP_REQUEST_CHANGE_FP_PWD\n");
        cmd0_data_ptr = (cmd0_data *)(cmd_ptr->data);
        cmd0_rsp_ptr = (cmd0_rsp *)(rsp_ptr->data);
        memcpy(cmd0_rsp_ptr->name, "cdfinger", 7);
        cmd0_rsp_ptr->featureID = 1;
        LOGD("CFP_NZ: ver:%d, age:%d\n", cmd0_data_ptr->ver, cmd0_data_ptr->age);
        LOGD("CFP_NZ: cmd0.name:%s, cmd0.featureID:%d\n", cmd0_rsp_ptr->name, cmd0_rsp_ptr->featureID);
        break;
    case FP_REQUEST_LOAD_FPALGO_PARAMS:
        LOGD("CFP_NZ: FP_REQUEST_LOAD_FPALGO_PARAMS\n");
        cmd0_data_ptr = (cmd0_data *)(cmd_ptr->data);
        cmd0_rsp_ptr = (cmd0_rsp *)(rsp_ptr->data);
        memcpy(cmd0_rsp_ptr->name, "cdfinger", 7);
        cmd0_rsp_ptr->featureID = 1;
        LOGD("CFP_NZ: ver:%d, age:%d\n", cmd0_data_ptr->ver, cmd0_data_ptr->age);
        LOGD("CFP_NZ: cmd0.name:%s, cmd0.featureID:%d\n", cmd0_rsp_ptr->name, cmd0_rsp_ptr->featureID);
        break;
    case FP_REQUEST_DRIVERTEST:
        break;
    case FP_REQUEST_GETSTATUS:
        break;
    case FP_REQUEST_CLEANSTATUS:
        break;

    case FP_REQUEST_ESDCHECK:
        break;
    case FP_REQUEST_FW_ISUPDATE:
        break;
    case FP_REQUEST_FW_UPDATEPRE:
        break;
    case FP_REQUEST_FW_UPDATE:
        break;
    case FP_REQUEST_DOWNLOAD_CFG:
        break;
    case FP_REQUEST_GETLBSTATUS:
        break;
    case FP_REQUEST_SETLBSTATUS:
        break;
    case FP_REQUEST_GETFORCEVALUE:
        break;
    case FP_REQUEST_CHECK_IMAGE_STA:
        break;
    case FP_REQUEST_UPDATE_TEMPLATE:
    {
        long long t0, t1, t2;
        uint8_t idx = 0;
        idx = cmd_ptr->data[0];
        LOGD("CFP_HS: FP_REQUEST_UPDATE_TEMPLATE: idx = %d", idx);

        LOGD("CFP_HS: FP_REQUEST_UPDATE_TEMPLATE, idx = %d, gCurrTemplatCnts[%d]", idx, gCurrTemplatCnts);

        WaitForAllTaskFinish();
        LOGD("CFP_HS: all recognize thread exit!!");

        if (idx > 0) // success
        {
            LOGD("glimmerLearn: index[%d] , g_whichOnePatch[%d], g_update[%d]", gRecogResult.whichOneTemplate, gRecogResult.whichOnePatch, gRecogResult.isUpdate);
            t0 = cfp_get_uptime();
            glimmerLearn(&gRecognSrc, gLearnData->pFpFtr, gLearnData->pMatchInfor, &(gFpRegisterRecords[idx - 1].pTemplate), 0, gRecogResult.whichOnePatch, gRecogResult.isUpdate);
            LOGT("CFP_HS: glimmerLearn spend %d ms", (uint32_t)(cfp_get_uptime() - t0));
            if (gRecogResult.isUpdate >= 0)
            {
                t0 = cfp_get_uptime();
                updateTemplate(&(gFpRegisterRecords[idx - 1].feature_id), idx - 1, gFpRegisterRecords[idx - 1].pTemplate);
                LOGT("CFP_HS: updateTemplate spend %d ms", (uint32_t)(cfp_get_uptime() - t0));
            }

            EndRecognition(&gLearnData);
        }

        gLearnData = NULL;

        { // update Calibration
            saveCalibrationPara(&(gBgImg.caliPara), gCaliParabuffer);
            //updateCalibrationParameter(gCaliParabuffer);
            LOGD("FP_REQUEST_UPDATE_TEMPLATE: saveCaliPara");
        }
    }
    break;
    case FP_REQUEST_PREPROCESS_INIT:
        break;

    case FP_REQUEST_READ_BYTES:
        break;
    case FP_REQUEST_WRITE_BYTES:
        break;
    case FP_REQUEST_DETECT_FINGER:
    {
        gDetectRun = 1;
        int *flag = (int *)cmd_ptr->data;
        int *cmd_ptr = (int *)cmd;
        int *rsp_ptr = (int *)rsp;
        int retval = 0;
        DeviceError de = DE_SUCCESS;

        *rsp_ptr = -1;
        LOGD("CFP_NZ: FP_REQUEST_DETECT_FINGER [%d]\n", *flag);
        de = cfp_devCheckFinger(*flag);
        LOGD("CFP_NZ: FP_REQUEST_DETECT_FINGER, result = %d", de);
        if (de != DE_SUCCESS)
        {
            if (de == DE_OPEN_FAILED || de == DE_IO_ERROR)
            {
                *rsp_ptr = -1;
                break;
            }
            else if (de == DE_NOT_FINGER)
            {
                *rsp_ptr = 0;
            }
            else if (de == DE_INCOMPLETE)
            {
                *rsp_ptr = 2;
            }
        }
        else
        {
            *rsp_ptr = 1;
        }

        break;
    }
    case FP_REQUEST_CANCEL_DETECT_FINGER:
    {
        gDetectRun = 0;
        break;
    }
    case FP_REQUEST_SET_ACTIVE_USER:
    {
        int userid = *(int *)(cmd_ptr->data);
        char *pPath = (char *)(rsp_ptr->data);

        pPath[rsplen] = '\0';

       // LOGE("Leosin-->testSetPath : cfp_main.c -> FP_REQUEST_SET_ACTIVE_USER path is ==> [%s]\n", pPath);

        if (gCurrentUser != userid)
        {
            LOGD("CFP_NZ: FP_REQUEST_SET_ACTIVE_USER => [%d]\n", userid);
            cfp_data_init(userid);
            gCurrentUser = userid;
        }
        else
        {
            LOGD("CFP_NZ: FP_REQUEST_SET_ACTIVE_USER already in user[%d]\n", gCurrentUser);
        }

        if (pPath != NULL)
        {
            memcpy(SET_PATH, pPath, rsplen);
           // LOGE("Leosin-->testSetPath : cfp_main.c ->  SET_PATH ==>  [%s]\n", pPath);
        }
        else
        {
          //  LOGE("Leosin-->testSetPath : cfp_main.c ->  path is null ==> [%s]\n", pPath);
        }

        break;
    }
    case FP_REQUEST_REINIT_TRIGGER:
    {
        int *rsp_ptr = (int *)rsp;
        *rsp_ptr = updateInterruptTriggerVal();
        break;
    }
    default:
        LOGE("CFP_NZ: default");
        break;
    }
#ifdef CFP_ENV_ANDROID
    pthread_mutex_unlock(&gCfpTeeMutex);
    LOGD("gCfpTeeMutex unlock-------------->>>>>>>");
#elif defined(CFP_ENV_UNIX) || defined(CFP_ENV_WIN)
#elif defined(CFP_ENV_TEE_QSEE)

#endif
}

/**
 @brief
 App specific shutdown
 App will be given a chance to shutdown gracefully
 */
void cfp_trustzone_shutdown(void)
{
    LOGD("CFP_NZ:SAMPLE App shutdown");
    return;
}

int recognizeByPolicy(uint8_t *buffer, uint32_t *ids, int32_t *whichOneTemplate, int32_t *whichOnePatch, int32_t *score, int32_t *isupdate)
{
    LOGD("recognizeByPolicy enter :TemplateIdx[%d], whichPatch[%d], score[%d], update[%d]", *whichOneTemplate, *whichOnePatch, *score, *isupdate);
    //LOGD("Leosin-->enroll : cfp_main.c -> cfp_devReadFingerImage(TemplateIdx[%d], whichPatch[%d], score[%d], update[%d])", *whichOneTemplate, *whichOnePatch, *score, *isupdate);

    long long t0, t1, t2 = 0;
    int i = 0;
    int loop = 1;
    int32_t template_count = 0;
    int gcmd_id_para = 0;

    memset(&gRecognSrc, 0x00, sizeof(gRecognSrc));
    gRecognSrc.layout.height = SENSOR_HEIGHT;
    gRecognSrc.layout.width = SENSOR_WIDTH;
    gRecognSrc.layout.bits = 8;
    gRecognSrc.ability = 1;
    gRecognSrc.layout.channels = 1;
    gRecognSrc.CT = 1;
    gRecognSrc.buffer = buffer;

    if (gcmd_id == FP_REQUEST_REGIDTER)
    {
        t0 = cfp_get_uptime();
        glimmer_Reg_ComputeFtr(&gRecognSrc, gRegisterSession);
        gcmd_id_para = GetEnrollSteps();
        LOGT("[CFP_HS]: glimmer_Reg_ComputeFtr Spend time: %d", (uint32_t)(cfp_get_uptime() - t0));
    }
    else
    {
        t0 = cfp_get_uptime();
        gLearnData = beginRecognition();
        LOGT("[CFP_HS]: beginRecognition Spend time: %d", (uint32_t)(cfp_get_uptime() - t0));
        t0 = cfp_get_uptime();
        glimmer_Rec_ComputeFtr(&gRecognSrc, gLearnData);
        LOGT("[CFP_HS]: glimmerComputeFtr Spend time: %d", (uint32_t)(cfp_get_uptime() - t0));
    }

    // memset(&gNewTemplate, 0x00, sizeof(gNewTemplate));
    for (i = 0; i < MAX_REGISTER_RECORD_NUM; i++)
    {
        if (gFpRegisterRecords[i].status == FPALGO_FEATURE_USED)
        {
            template_count++;
        }
    }

    SetActiveTaskNumber(template_count);
    if (template_count == 0)
        return -1;

    ResetActiveTaskContext(buffer, gcmd_id, gcmd_id_para);

    {

        ActiveAllTaskByOrder();

        int taskIndex = WaitForFirstSuccessTask();
        Recognize_Info_t *pRecInfo = GetRecognizeInfoByIndex(taskIndex);
        if (pRecInfo != NULL)
        {
            *whichOneTemplate = taskIndex;
            *whichOnePatch = pRecInfo->whichOnePatch;
            *score = pRecInfo->score;
            *isupdate = pRecInfo->isUpdate;
        }
    }

    LOGD("recognizeByPolicy end :TemplateIdx[%d], whichPatch[%d], score[%d], update[%d]", *whichOneTemplate, *whichOnePatch, *score, *isupdate);
    if (*score > 0)
    {
        return 0;
    }

    return -1;
}
