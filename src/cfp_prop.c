#include "cfp_prop.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef DEFAULT_PROP_FINGERDOWN_VALUE
#undef DEFAULT_PROP_FINGERDOWN_VALUE
#endif
#define DEFAULT_PROP_FINGERDOWN_VALUE   "8"

#ifdef DEFAULT_PROP_FINGERUP_VALUE
#undef DEFAULT_PROP_FINGERUP_VALUE
#endif
#define DEFAULT_PROP_FINGERUP_VALUE     "8"

#ifdef DEFAULT_PROP_ENROLL_STEPS
#undef DEFAULT_PROP_ENROLL_STEPS
#endif
#define DEFAULT_PROP_ENROLL_STEPS       "8"

#ifdef DEFAULT_PROP_ENROLL_OVERLAP
#undef DEFAULT_PROP_ENROLL_OVERLAP
#endif
#define DEFAULT_PROP_ENROLL_OVERLAP       "50"

////////////////////// interface //////////////////////////////
char * GetPrivateRoot()
{
    static char tmpPath[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_RO_ROPRIVATE_ROOT, tmpPath, DEFAULT_PROP_PRIVATE_ROOT);
    return tmpPath;
}

char * GetPrivateRoot2()
{
    static char tmpPath[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_RO_ROPRIVATE_ROOT, tmpPath, DEFAULT_PROP_PRIVATE_ROOT);
    return tmpPath;
}

char * GetCalibrationFile()
{
    static char tmpPath[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_RO_CALIBRATION_ROOT, tmpPath, DEFAULT_PROP_CALIBRATION_ROOT);
    sprintf(tmpPath, "%s/.cdfinger", tmpPath);
    return tmpPath;
}

char * GetCalibrationFile2()
{
    static char tmpPath[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_RO_CALIBRATION_ROOT, tmpPath, DEFAULT_PROP_CALIBRATION_ROOT);
    sprintf(tmpPath, "%s/.algocalib", tmpPath);
    return tmpPath;
}

int IsDebugEnbaled()
{
    char val[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_DEBUG_ENABLE, val, DEFAULT_PROP_VALUE);
    if(strcmp(val, DEFAULT_PROP_ENABLE) == 0)
    {
        //LOGD("Debug enabled!");
        return 1;
    }

    return 0;
}

int IsNeedSaveImg()
{
    char val[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_SAVEIMAGE_ENABLE, val, DEFAULT_PROP_VALUE);
    if(strcmp(val, DEFAULT_PROP_ENABLE_VALUE) == 0)
    {
        //LOGD("Save image enabled!");
        return 1;
    }

    return 0;
}

int GetFingerDownThresholdValue()
{
    char val[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_FINGER_DOWN_VALUE, val, DEFAULT_PROP_FINGERDOWN_VALUE);
    return atoi(val);
}

int GetFingerUpThresholdValue()
{
    char val[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_FINGER_UP_VALUE, val, DEFAULT_PROP_FINGERUP_VALUE);
    return atoi(val);
}

int IsPlatformAndroidN()
{
    char val[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_PLATFORM_BUILD_VERSION, val, "");
    int i = 0;
    for(i = 0; i < strlen(val); ++i)
    {
        if(isdigit(val[i]))
        {
            if(val[i] == '7') return 1;
            else return 0;
        }
    }
    return 0;
}

int IsHDREnabled()
{
	char val[PROPERTY_VALUE_MAX] = {0};
	property_get(PROP_HDR_ENABLE, val, DEFAULT_PROP_VALUE);
	if(strcmp(val, DEFAULT_PROP_ENABLE_VALUE) == 0)
	{
	    //LOGD("HDR enabled!");
	    return 1;
	}

	return 0;
}

int GetEnrollSteps()
{
    char val[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_ENROLL_STEPS, val, DEFAULT_PROP_ENROLL_STEPS);
    return atoi(val);
}

int GetEnrollHighOverlayProgressSteps()
{
    char val[PROPERTY_VALUE_MAX] = {0};
    property_get(PROP_ENROLL_STEPS, val, DEFAULT_PROP_ENROLL_STEPS);
    return 0;//(atoi(val)/3+1);
}

int GetEnrollOverlapThr()
{
    return atoi(DEFAULT_PROP_ENROLL_OVERLAP);
}


