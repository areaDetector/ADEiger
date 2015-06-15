/* eigerDetector.cpp
 *
 * This is a driver for a Eiger pixel array detector.
 *
 * Authors: Bruno Martins
 *          Diego Omitto
 *          Brookhaven National Laboratory
 *
 * Created: March 30, 2015
 *
 */

#include <epicsExport.h>
#include <epicsThread.h>
#include <iocsh.h>
#include <asynOctetSyncIO.h>
#include <math.h>

#include <vector>
#include <hdf5.h>
#include <hdf5_hl.h>
#include <iostream>

#include <frozen.h> // JSON parser

#include "ADDriver.h"

/** Messages to/from server */
#define GET_FILE_RETRIES    10
#define MAX_MESSAGE_SIZE    65536
#define MAX_BUF_SIZE        256
#define MAX_JSON_TOKENS     100
#define DEF_TIMEOUT         2.0
#define DEF_TIMEOUT_INIT    30.0
#define DEF_TIMEOUT_ARM     55.0
#define DEF_TIMEOUT_CMD     5.0
#define CHUNK_SIZE          (MAX_MESSAGE_SIZE-512)

#define ID_STR          "$id"
#define ID_LEN          3

#define API_VERSION     "1.0.4"
#define EOL             "\r\n"      // End of Line
#define EOH             EOL EOL     // End of Header
#define EOH_LEN         4           // End of Header Length

#define DATA_NATIVE "application/json; charset=utf-8"
#define DATA_TIFF   "application/tiff"
#define DATA_HDF5   "application/hdf5"
#define DATA_HTML   "text/html"

#define REQUEST_GET\
    "GET %s%s HTTP/1.0" EOL \
    "Accept: " DATA_NATIVE EOH

#define REQUEST_PUT\
    "PUT %s%s HTTP/1.0" EOL \
    "Accept-Encoding: identity" EOL\
    "Content-Type: " DATA_NATIVE EOL \
    "Content-Length: %u" EOH

#define REQUEST_HEAD\
    "HEAD %s%s HTTP/1.0" EOH

#define REQUEST_GET_PARTIAL\
    "GET %s%s HTTP/1.0" EOL \
    "Range: bytes=%lu-%lu" EOH

#define ERR(msg) asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s %s\n", \
    driverName, functionName, msg)

#define ERR_ARGS(fmt,...) asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, \
    "%s:%s "fmt"\n", driverName, functionName, __VA_ARGS__);

/** Subsystems */
typedef enum
{
    SSAPIVersion,
    SSDetConfig,
    SSDetStatus,
    SSFWConfig,
    SSFWStatus,
    SSCommand,
    SSData,

    SSCount,
} eigerSys;

typedef enum
{
    FWDisabled,
    FWEnabled,

    FWModeCount
} eigerFWMode;

typedef enum {
    TMInternalSeries,   // INTS
    TMInternalEnable,   // INTE
    TMExternalSeries,   // EXTS
    TMExternalEnable,   // EXTE

    TMCount
} eigerTriggerMode;

static const char *eigerSysStr [SSCount] = {
    "/detector/api/version",
    "/detector/api/"   API_VERSION "/config/",
    "/detector/api/"   API_VERSION "/status/",
    "/filewriter/api/" API_VERSION "/config/",
    "/filewriter/api/" API_VERSION "/status/",
    "/detector/api/"   API_VERSION "/command/",
    "/data/",
};

static const char *eigerFWModeStr [FWModeCount] = {
    "disabled", "enabled"
};

static const char *eigerTMStr [TMCount] = {
    "ints", "inte", "exts", "exte"
};

struct response
{
    int code;
    char *data;
    size_t size;
    size_t contentLength;
};

static const char *driverName = "eigerDetector";

/* FileWriter Config Parameters */
#define EigerFWClearString              "CLEAR"
#define EigerFWCompressionString        "COMPRESSION"
#define EigerFWImageNrStartString       "IMAGE_NR_START"
#define EigerFWModeString               "MODE"
#define EigerFWNamePatternString        "NAME_PATTERN"
#define EigerFWNImgsPerFileString       "NIMAGES_PER_FILE"

/* Detector Config Parameters */
#define EigerBeamXString                "BEAM_X"
#define EigerBeamYString                "BEAM_Y"
#define EigerDetDistString              "DET_DIST"
#define EigerFlatfieldString            "FLATFIELD_APPLIED"
#define EigerPhotonEnergyString         "PHOTON_ENERGY"
#define EigerThresholdString            "THRESHOLD"
#define EigerWavelengthString           "WAVELENGTH"

/* Status */
#define EigerThTemp0String              "TH_TEMP_0"
#define EigerThHumid0String             "TH_HUMID_0"
#define EigerLink0String                "LINK_0"
#define EigerLink1String                "LINK_1"
#define EigerLink2String                "LINK_2"
#define EigerLink3String                "LINK_3"

/* Other */
#define EigerArmedString                "ARMED"
#define EigerDisarmString               "DISARM"
#define EigerSaveFilesString            "SAVE_FILES"
#define EigerSequenceIdString           "SEQ_ID"

/** Driver for Dectris Eiger pixel array detectors using their REST server */
class eigerDetector : public ADDriver
{
public:
    eigerDetector(const char *portName, const char *serverPort, int maxBuffers,
            size_t maxMemory, int priority, int stackSize);

    /* These are the methods that we override from ADDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus writeOctet(asynUser *pasynUser, const char *value,
                                    size_t nChars, size_t *nActual);
    void report(FILE *fp, int details);

    /* This should be private but is called from C so must be public */
    void eigerTask();

protected:
    /* FileWriter Config Parameters */
    int EigerFWClear;
    #define FIRST_EIGER_PARAM EigerFWClear
    int EigerFWCompression;
    int EigerFWImageNrStart;
    int EigerFWMode;
    int EigerFWNamePattern;
    int EigerFWNImgsPerFile;

    /* Detector Config Parameters */
    int EigerBeamX;
    int EigerBeamY;
    int EigerDetDist;
    int EigerFlatfield;
    int EigerPhotonEnergy;
    int EigerThreshold;
    int EigerWavelength;

    /* Detector Status Parameters */
    int EigerThTemp0;
    int EigerThHumid0;
    int EigerLink0;
    int EigerLink1;
    int EigerLink2;
    int EigerLink3;

    /* Other parameters */
    int EigerArmed;
    int EigerDisarm;
    int EigerSaveFiles;
    int EigerSequenceId;
    #define LAST_EIGER_PARAM EigerSequenceId

private:
    epicsEvent disarmEvent;
    epicsEventId startEventId;
    epicsEventId stopEventId;
    char toServer[MAX_MESSAGE_SIZE];
    char fromServer[MAX_MESSAGE_SIZE];
    asynUser *pasynUserServer;

    /* These are the methods that are new to this class */

    /*
     * Basic HTTP communication
     */
    asynStatus doRequest  (size_t requestSize, struct response *response,
            double timeout = DEF_TIMEOUT);

    /*
     * Get/set parameter value (string form)
     */
    asynStatus get (eigerSys sys, const char *param, char *value, size_t len,
            double timeout = DEF_TIMEOUT);
    asynStatus put (eigerSys sys, const char *param, const char *value,
            size_t len, double timeout = DEF_TIMEOUT);
    asynStatus parsePutResponse (struct response response);

    /*
     * Nice wrappers to set/get parameters
     */
    asynStatus getString  (eigerSys sys, const char *param, char *value,
            size_t len);
    asynStatus getInt     (eigerSys sys, const char *param, int *value);
    asynStatus getDouble  (eigerSys sys, const char *param, double *value);
    asynStatus getBool    (eigerSys sys, const char *param, bool *value);

    asynStatus getStringP (eigerSys sys, const char *param, int dest);
    asynStatus getIntP    (eigerSys sys, const char *param, int dest);
    asynStatus getDoubleP (eigerSys sys, const char *param, int dest);
    asynStatus getBoolP   (eigerSys sys, const char *param, int dest);

    asynStatus putString  (eigerSys sys, const char *param, const char *value);
    asynStatus putInt     (eigerSys sys, const char *param, int value);
    asynStatus putDouble  (eigerSys sys, const char *param, double value);
    asynStatus putBool    (eigerSys sys, const char *param, bool value);

    /*
     * Send a command to the detector
     */
    asynStatus command (const char *name, double timeout = DEF_TIMEOUT_CMD);

    /*
     * File getters
     */
    asynStatus getFileSize   (const char *remoteFile, size_t *len);
    asynStatus getFile       (const char *remoteFile, char **data, size_t *len);
    asynStatus saveFile      (const char *file, char *data, size_t len);

    /*
     * Arm, trigger and disarm
     */
    asynStatus capture (eigerTriggerMode triggerMode, double triggerTimeout);

    /*
     * Download detector files locally and publish as NDArrays
     */
    asynStatus downloadAndPublish (void);

    /*
     * HDF5 helpers
     */
    asynStatus parseH5File  (char *buf, size_t len);

    /*
     * Read some detector status parameters
     */
    asynStatus eigerStatus (void);
};

#define NUM_EIGER_PARAMS ((int)(&LAST_EIGER_PARAM - &FIRST_EIGER_PARAM + 1))

static void eigerTaskC (void *drvPvt)
{
    eigerDetector *pPvt = (eigerDetector *)drvPvt;
    pPvt->eigerTask();
}

/** Constructor for Eiger driver; most parameters are simply passed to
  * ADDriver::ADDriver.
  * After calling the base class constructor this method creates a thread to
  * collect the detector data, and sets reasonable default values for the
  * parameters defined in this class, asynNDArrayDriver, and ADDriver.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] serverPort The name of the asyn port previously created with
  *            drvAsynIPPortConfigure to communicate with server.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] maxBuffers The maximum number of NDArray buffers that the
  *            NDArrayPool for this driver is allowed to allocate. Set this to
  *            -1 to allow an unlimited number of buffers.
  * \param[in] maxMemory The maximum amount of memory that the NDArrayPool for
  *            this driver is allowed to allocate. Set this to -1 to allow an
  *            unlimited amount of memory.
  * \param[in] priority The thread priority for the asyn port driver thread if
  *            ASYN_CANBLOCK is set in asynFlags.
  * \param[in] stackSize The stack size for the asyn port driver thread if
  *            ASYN_CANBLOCK is set in asynFlags.
  */
eigerDetector::eigerDetector (const char *portName, const char *serverPort,
        int maxBuffers, size_t maxMemory, int priority,
        int stackSize)

    : ADDriver(portName, 1, NUM_EIGER_PARAMS, maxBuffers, maxMemory,
               0, 0,             /* No interfaces beyond ADDriver.cpp */
               ASYN_CANBLOCK,    /* ASYN_CANBLOCK=1, ASYN_MULTIDEVICE=0 */
               1,                /* autoConnect=1 */
               priority, stackSize),
    disarmEvent(epicsEventEmpty)
{
    int status = asynSuccess;
    const char *functionName = "eigerDetector";

    /* Connect to REST server */
    status = pasynOctetSyncIO->connect(serverPort, 0, &pasynUserServer, NULL);

    /* Create the epicsEvents for signaling to the eiger task when acquisition
     * starts and stops */
    startEventId = epicsEventCreate(epicsEventEmpty);
    if(!startEventId)
    {
        ERR("epicsEventCreate failure for start event");
        return;
    }

    stopEventId = epicsEventCreate(epicsEventEmpty);
    if(!stopEventId)
    {
        ERR("epicsEventCreate failure for stop event");
        return;
    }

    createParam(EigerFWClearString,       asynParamInt32, &EigerFWClear);
    createParam(EigerFWCompressionString, asynParamInt32, &EigerFWCompression);
    createParam(EigerFWImageNrStartString,asynParamInt32, &EigerFWImageNrStart);
    createParam(EigerFWModeString,        asynParamInt32, &EigerFWMode);
    createParam(EigerFWNamePatternString, asynParamOctet, &EigerFWNamePattern);
    createParam(EigerFWNImgsPerFileString,asynParamInt32, &EigerFWNImgsPerFile);

    createParam(EigerBeamXString,         asynParamFloat64, &EigerBeamX);
    createParam(EigerBeamYString,         asynParamFloat64, &EigerBeamY);
    createParam(EigerDetDistString,       asynParamFloat64, &EigerDetDist);
    createParam(EigerFlatfieldString,     asynParamInt32,   &EigerFlatfield);
    createParam(EigerPhotonEnergyString,  asynParamFloat64, &EigerPhotonEnergy);
    createParam(EigerThresholdString,     asynParamFloat64, &EigerThreshold);
    createParam(EigerWavelengthString,    asynParamFloat64, &EigerWavelength);

    /* Detector Status Parameters */
    createParam(EigerThTemp0String,       asynParamFloat64, &EigerThTemp0);
    createParam(EigerThHumid0String,      asynParamFloat64, &EigerThHumid0);
    createParam(EigerLink0String,         asynParamInt32,   &EigerLink0);
    createParam(EigerLink1String,         asynParamInt32,   &EigerLink1);
    createParam(EigerLink2String,         asynParamInt32,   &EigerLink2);
    createParam(EigerLink3String,         asynParamInt32,   &EigerLink3);

    /* Other parameters */
    createParam(EigerArmedString,         asynParamInt32,   &EigerArmed);
    createParam(EigerDisarmString,        asynParamInt32,   &EigerDisarm);
    createParam(EigerSaveFilesString,     asynParamInt32,   &EigerSaveFiles);
    createParam(EigerSequenceIdString,    asynParamInt32,   &EigerSequenceId);

    status = asynSuccess;

    /* Set some default values for parameters */
    char desc[MAX_BUF_SIZE] = "";
    char *manufacturer, *space, *model;

    if(getString(SSDetConfig, "description", desc, sizeof(desc)))
    {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Eiger seems to be uninitialized\n"
                "Initializing... (may take a while)\n",
                driverName, functionName);

        if(command("initialize", DEF_TIMEOUT_INIT))
        {
            ERR("Eiger FAILED TO INITIALIZE");
            return;
        }

        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s Eiger initialized\n",
                    driverName, functionName);
    }

    status = getString(SSDetConfig, "description", desc, sizeof(desc));

    // Assume 'description' is of the form 'Dectris Eiger 1M'
    space = strchr(desc, ' ');
    *space = '\0';
    manufacturer = desc;
    model = space + 1;

    status |= setStringParam (ADManufacturer, manufacturer);
    status |= setStringParam (ADModel, model);

    int maxSizeX, maxSizeY;
    status |= getInt(SSDetConfig, "x_pixels_in_detector", &maxSizeX);
    status |= getInt(SSDetConfig, "y_pixels_in_detector", &maxSizeY);

    status |= setIntegerParam(ADMaxSizeX, maxSizeX);
    status |= setIntegerParam(ADMaxSizeY, maxSizeY);
    status |= setIntegerParam(ADSizeX, maxSizeX);
    status |= setIntegerParam(ADSizeY, maxSizeY);
    status |= setIntegerParam(NDArraySizeX, maxSizeX);
    status |= setIntegerParam(NDArraySizeY, maxSizeY);

    // Only internal trigger is supported at this time
    status |= setIntegerParam(ADTriggerMode, TMInternalSeries);
    status |= putString(SSDetConfig, "trigger_mode", eigerTMStr[TMInternalSeries]);

    char fwMode[MAX_BUF_SIZE];
    status |= getString(SSFWConfig, "mode", fwMode, sizeof(fwMode));
    status |= setIntegerParam(EigerFWMode, (int)(fwMode[0] == 'e'));

    status |= getDoubleP(SSDetConfig, "count_time",       ADAcquireTime);
    status |= getDoubleP(SSDetConfig, "frame_time",       ADAcquirePeriod);
    status |= getIntP   (SSDetConfig, "nimages",          ADNumImages);
    status |= getDoubleP(SSDetConfig, "photon_energy",    EigerPhotonEnergy);
    status |= getDoubleP(SSDetConfig, "threshold_energy", EigerThreshold);

    status |= getBoolP  (SSFWConfig, "compression_enabled",EigerFWCompression);
    status |= getIntP   (SSFWConfig, "image_nr_start",     EigerFWImageNrStart);
    status |= getStringP(SSFWConfig, "name_pattern",       EigerFWNamePattern);
    status |= getIntP   (SSFWConfig, "nimages_per_file",   EigerFWNImgsPerFile);

    status |= getDoubleP(SSDetConfig, "beam_center_x",     EigerBeamX);
    status |= getDoubleP(SSDetConfig, "beam_center_y",     EigerBeamY);
    status |= getDoubleP(SSDetConfig, "detector_distance", EigerDetDist);
    status |= getBoolP  (SSDetConfig, "flatfield_correction_applied",
            EigerFlatfield);
    status |= getDoubleP(SSDetConfig, "threshold_energy",  EigerThreshold);
    status |= getDoubleP(SSDetConfig, "wavelength",        EigerWavelength);

    status |= getDoubleP(SSDetStatus, "board_000/th0_temp",     EigerThTemp0);
    status |= getDoubleP(SSDetStatus, "board_000/th0_humidity", EigerThHumid0);

    status |= setIntegerParam(NDArraySize, 0);
    status |= setIntegerParam(NDDataType,  NDUInt32);
    status |= setIntegerParam(ADImageMode, ADImageMultiple);
    status |= setIntegerParam(EigerArmed,  0);
    status |= setIntegerParam(EigerSaveFiles, 1);
    status |= setIntegerParam(EigerSequenceId, 0);

    callParamCallbacks();

    // Auto Summation should always be true (Eiger API Reference v1.1pre)
    status |= putBool(SSDetConfig, "auto_summation", true);

    if(status)
    {
        ERR("unable to set camera parameters");
        return;
    }

    /* Create the thread that updates the images */
    status = (epicsThreadCreate("eigerDetTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)eigerTaskC, this) == NULL);

    if(status)
    {
        ERR("epicsThreadCreate failure for image task");
        return;
    }
}

/** Called when asyn clients call pasynInt32->write().
  * This function performs actions for some parameters, including ADAcquire,
  * ADTriggerMode, etc.
  * For all parameters it sets the value in the parameter library and calls any
  * registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus eigerDetector::writeInt32 (asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeInt32";
    int adstatus;

    if (function == EigerFWClear)
        status = putInt(SSFWConfig, "clear", 1);
    else if (function == EigerFWCompression)
        status = putBool(SSFWConfig, "compression_enabled", (bool)value);
    else if (function == EigerFWImageNrStart)
        status = putInt(SSFWConfig, "image_nr_start", value);
    else if (function == EigerFWMode)
        status = putString(SSFWConfig, "mode", eigerFWModeStr[value]);
    else if (function == EigerFWNImgsPerFile)
        status = putInt(SSFWConfig, "nimages_per_file", value);
    else if (function == EigerFlatfield)
        status = putBool(SSDetConfig, "flatfield_correction_applied", (bool)value);
    else if (function == ADTriggerMode)
        status = putString(SSDetConfig, "trigger_mode", eigerTMStr[value]);
    else if (function == ADNumImages)
        status = putInt(SSDetConfig, "nimages", value);
    else if (function == ADReadStatus)
        status = eigerStatus();
    else if (function == ADAcquire)
    {
        getIntegerParam(ADStatus, &adstatus);

        if (value && (adstatus == ADStatusIdle || adstatus == ADStatusError ||
                adstatus == ADStatusAborted))
        {
            setStringParam(ADStatusMessage, "Acquiring data");
            setIntegerParam(ADStatus, ADStatusAcquire);
        }

        if (!value && adstatus == ADStatusAcquire)
        {
            setStringParam(ADStatusMessage, "Acquisition aborted");
            setIntegerParam(ADStatus, ADStatusAborted);
        }
    }
    else if (function == EigerDisarm)
    {
        status = command("disarm");
        if(!status)
            disarmEvent.signal();
    }
    else if(function < FIRST_EIGER_PARAM)
        status = ADDriver::writeInt32(pasynUser, value);

    if(status)
    {
        ERR_ARGS("error status=%d function=%d, value=%d", status, function, value);
        return status;
    }

    status = setIntegerParam(function, value);
    callParamCallbacks();

    // Effectively start/stop acquisition if that's the case
    if (function == ADAcquire)
    {
        if (value && (adstatus == ADStatusIdle || adstatus == ADStatusError ||
                adstatus == ADStatusAborted))
        {
            epicsEventSignal(this->startEventId);
        }
        else if (!value && (adstatus == ADStatusAcquire))
        {
            epicsEventSignal(this->stopEventId);
        }
    }

    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s:%s: error, status=%d function=%d, value=%d\n",
              driverName, functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s:%s: function=%d, value=%d\n",
              driverName, functionName, function, value);

    return status;
}

/** Called when asyn clients call pasynFloat64->write().
  * This function performs actions for some parameters, including ADAcquireTime,
  * ADGain, etc.
  * For all parameters it sets the value in the parameter library and calls any
  * registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and
  *            address.
  * \param[in] value Value to write. */
asynStatus eigerDetector::writeFloat64 (asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeFloat64";

    if (function == EigerBeamX)
        status = putDouble(SSDetConfig, "beam_center_x", value);
    else if (function == EigerBeamY)
        status = putDouble(SSDetConfig, "beam_center_y", value);
    else if (function == EigerDetDist)
        status = putDouble(SSDetConfig, "detector_distance", value);
    else if (function == EigerPhotonEnergy)
        status = putDouble(SSDetConfig, "photon_energy", value);
    else if (function == EigerThreshold)
        status = putDouble(SSDetConfig, "threshold_energy", value);
    else if (function == EigerWavelength)
        status = putDouble(SSDetConfig, "wavelength", value);
    else if (function == ADAcquireTime)
        status = putDouble(SSDetConfig, "count_time", value);
    else if (function == ADAcquirePeriod)
        status = putDouble(SSDetConfig, "frame_time", value);
    else if (function < FIRST_EIGER_PARAM)
        status = ADDriver::writeFloat64(pasynUser, value);

    if (status)
    {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s:%s error, status=%d function=%d, value=%f\n",
              driverName, functionName, status, function, value);
    }
    else
    {
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s:%s: function=%d, value=%f\n",
              driverName, functionName, function, value);

        /* Do callbacks so higher layers see any changes */
        setDoubleParam(function, value);
        callParamCallbacks();
    }
    return status;
}

/** Called when asyn clients call pasynOctet->write().
  * This function performs actions for some parameters, including eigerBadPixelFile, ADFilePath, etc.
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Address of the string to write.
  * \param[in] nChars Number of characters to write.
  * \param[out] nActual Number of characters actually written. */
asynStatus eigerDetector::writeOctet (asynUser *pasynUser, const char *value,
                                    size_t nChars, size_t *nActual)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeOctet";

    if (function == EigerFWNamePattern)
        putString(SSFWConfig, "name_pattern", value);
    else if (function < FIRST_EIGER_PARAM)
        status = ADDriver::writeOctet(pasynUser, value, nChars, nActual);

    status = setStringParam(function, value);
    callParamCallbacks();

    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "%s:%s: status=%d, function=%d, value=%s",
                  driverName, functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s:%s: function=%d, value=%s\n",
              driverName, functionName, function, value);

    *nActual = nChars;
    return status;
}

/** Report status of the driver.
  * Prints details about the driver if details>0.
  * It then calls the ADDriver::report() method.
  * \param[in] fp File pointed passed by caller where the output is written to.
  * \param[in] details If >0 then driver details are printed.
  */
void eigerDetector::report (FILE *fp, int details)
{
    fprintf(fp, "Eiger detector %s\n", this->portName);
    if (details > 0) {
        int nx, ny, dataType;
        getIntegerParam(ADSizeX, &nx);
        getIntegerParam(ADSizeY, &ny);
        getIntegerParam(NDDataType, &dataType);
        fprintf(fp, "  NX, NY:            %d  %d\n", nx, ny);
        fprintf(fp, "  Data type:         %d\n", dataType);
    }
    /* Invoke the base class method */
    ADDriver::report(fp, details);
}

/** This thread controls acquisition, reads image files to get the image data,
  * and does the callbacks to send it to higher layers */
void eigerDetector::eigerTask (void)
{
    const char *functionName = "eigerTask";
    int status = asynSuccess;

    this->lock();

    for(;;)
    {
        int acquire;
        getIntegerParam(ADAcquire, &acquire);

        if (!acquire)
        {
            if (!status)
                setStringParam(ADStatusMessage, "Waiting for acquire command");

            callParamCallbacks();

            this->unlock(); // Wait for semaphore unlocked

            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: waiting for acquire to start\n",
                    driverName, functionName);

            status = epicsEventWait(this->startEventId);   // Wait for semaphore
            this->lock();
        }

        int saveFiles;
        getIntegerParam(EigerSaveFiles, &saveFiles);

        if(saveFiles)
        {
            int filePathExists;
            checkPath();
            getIntegerParam(NDFilePathExists, &filePathExists);
            if(!filePathExists)
            {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s:%s: invalid local File Path\n",
                        driverName, functionName);
                status = asynError;
                setIntegerParam(ADStatus, ADStatusError);
                setStringParam(ADStatusMessage, "Invalid file path");
                goto end;
            }
        }

        /*
         * Acquire
         */
        double acquirePeriod, triggerTimeout;
        int numImages, triggerMode;

        getDoubleParam(ADAcquirePeriod, &acquirePeriod);
        getIntegerParam(ADNumImages, &numImages);
        getIntegerParam(ADTriggerMode, &triggerMode);

        triggerTimeout = triggerMode == TMInternalSeries ? acquirePeriod*numImages + 10.0 : 0.0;

        setIntegerParam(ADStatus, ADStatusAcquire);
        setShutter(1);
        status = capture((eigerTriggerMode) triggerMode, triggerTimeout);
        setShutter(0);

        if(status)
        {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: underlying capture failed\n",
                    driverName, functionName);

            setIntegerParam(ADStatus, ADStatusError);
            goto end;
        }

        /*
         * Download and publish
         */
        if((status = downloadAndPublish()))
        {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: underlying downloadAndPublish failed\n",
                    driverName, functionName);
            setIntegerParam(ADStatus, ADStatusError);
            setStringParam(ADStatusMessage, "Download failed");
        }

end:
        /* If everything was ok, set the status back to idle */
        int statusParam = 0;
        getIntegerParam(ADStatus, &statusParam);

        if (!status)
            setIntegerParam(ADStatus, ADStatusIdle);
        else if (statusParam != ADStatusAborted)
            setIntegerParam(ADStatus, ADStatusError);

        setIntegerParam(ADAcquire, 0);
        callParamCallbacks();
    }
}

asynStatus eigerDetector::doRequest (size_t requestSize,
        struct response *response, double timeout)
{
    const char *functionName = "doRequest";
    asynStatus status;
    size_t nwrite, nread;
    int eomReason, scanned;

    setStringParam(ADStringToServer, toServer);
    setStringParam(ADStringFromServer, "");
    callParamCallbacks();

    // Send request / get response
    status = pasynOctetSyncIO->writeRead(pasynUserServer,
            toServer, requestSize, fromServer, sizeof(fromServer),
            timeout, &nwrite, &nread, &eomReason);
    if(status)
    {
        ERR_ARGS("send/recv failed\n[%s]", pasynUserServer->errorMessage);
        return status;
    }

    // Find Content-Length (useful for HEAD requests)
    char *contentLength = strcasestr(fromServer, "Content-Length");
    if(!contentLength)
    {
        ERR("malformed packet: no Content-Length");
        return asynError;
    }

    scanned = sscanf(contentLength, "%*s %lu", &response->contentLength);
    if(scanned != 1)
    {
        ERR("malformed packet: couldn't parse Content-Length");
        return asynError;
    }

    // Find end of header
    char *eoh = strstr(fromServer, EOH);
    if(!eoh)
    {
        ERR("malformed packet: no End of Header");
        return asynError;
    }

    // Fill response
    scanned = sscanf(fromServer, "%*s %d", &response->code);
    if(scanned != 1)
    {
        ERR("malformed packet: couldn't parse response code");
        return asynError;
    }

    response->data = eoh + EOH_LEN;
    response->size = nread - (size_t)(response->data-fromServer);

    if(response->size < sizeof(fromServer))
        response->data[response->size] = '\0';

    setStringParam(ADStringFromServer, fromServer);
    callParamCallbacks();

    return asynSuccess;
}

asynStatus eigerDetector::get (eigerSys sys, const char *param, char *value,
        size_t len, double timeout)
{
    const char *functionName = "get";
    const char *reqFmt = REQUEST_GET;
    const char *url = eigerSysStr[sys];
    asynStatus status;
    size_t reqSize;
    struct response response;
    int err;

    reqSize = snprintf(toServer, sizeof(toServer), reqFmt, url, param);

    status = doRequest(reqSize, &response, timeout);
    if(status)
    {
        ERR_ARGS("[param=%s] request failed", param);
        return status;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[param=%s] server returned error code %d", param, response.code);
        return asynError;
    }

    if(!value)
        return asynSuccess;

    struct json_token tokens[MAX_JSON_TOKENS];
    struct json_token *valueToken;

    err = parse_json(response.data, response.size, tokens, MAX_JSON_TOKENS);
    if(err < 0)
    {
        ERR_ARGS("[param=%s] unable to parse json response\n[%.*s]",
            param, (int)response.size, response.data);
        return asynError;
    }

    valueToken = find_json_token(tokens, "value");
    if(valueToken == NULL)
    {
        ERR_ARGS("[param=%s] unable to find 'value' json field", param);
        return asynError;
    }

    if((size_t)valueToken->len > ((size_t)(len + 1)))
    {
        ERR_ARGS("[param=%s] destination buffer is too short", param);
        return asynError;
    }

    memcpy((void*)value, (void*)valueToken->ptr, valueToken->len);
    value[valueToken->len] = '\0';

    return asynSuccess;
}

asynStatus eigerDetector::put (eigerSys sys, const char *param,
        const char *value, size_t len, double timeout)
{
    const char *functionName = "put";
    const char *reqFmt = REQUEST_PUT ;
    const char *url = eigerSysStr[sys];
    asynStatus status;
    size_t reqSize, remaining;
    struct response response;

    reqSize = snprintf(toServer, sizeof(toServer), reqFmt, url, param, len);
    remaining = sizeof(toServer) - reqSize;

    if(remaining < len)
    {
        ERR_ARGS("[param=%s] toServer buffer is not big enough", param);
        return asynError;
    }

    if(len && value)
    {
        memcpy(toServer + reqSize, value, len);
        reqSize += len;
        remaining -= len;
    }

    if(remaining)
        *(toServer+reqSize) = '\0';     // Make ADStringToServer printable

    status = doRequest(reqSize, &response, timeout);
    if(status)
    {
        ERR_ARGS("[param=%s] request failed", param);
        return status;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[param=%s] server returned error code %d", param, response.code);
        return asynError;
    }

    if(response.size)
    {
        status = parsePutResponse(response);
        if(status)
        {
            ERR_ARGS("[param=%s] unable to parse response", param);
            return status;
        }
    }

    return asynSuccess;
}

asynStatus eigerDetector::parsePutResponse(struct response response)
{
    // Try to parse the response
    // Two possibilities:
    //   Response to PUT to a parameter: list of changed values
    //   Response to the arm command: sequence id
    const char *functionName = "parsePutResponse";
    int err;
    asynStatus status = asynSuccess;

    // Copy response data locally (may be overwritten by GETs)
    char responseData[response.size];
    memcpy(responseData, response.data, response.size);

    struct json_token tokens[MAX_JSON_TOKENS];
    err = parse_json(responseData, response.size, tokens, MAX_JSON_TOKENS);
    if(err < 0)
    {
        ERR("unable to parse response json");
        return asynError;
    }

    if(tokens[0].type == JSON_TYPE_OBJECT)  // sequence id or series id
    {
        struct json_token *seqIdToken = find_json_token(tokens, "sequence id");
        if(!seqIdToken)
        {
            ERR("unable to find 'sequence id' token, will try 'series id'");

            seqIdToken = find_json_token(tokens, "series id");
            if(!seqIdToken)
            {
                ERR("unable to find 'series id' token");
                return asynError;
            }
        }

        int seqId;
        int scanned = sscanf(seqIdToken->ptr, "%d", &seqId);
        if(scanned != 1)
        {
            ERR("unable to parse 'sequence_id' token");
            return asynError;
        }

        setIntegerParam(EigerSequenceId, seqId);
        callParamCallbacks();
    }
    else if(tokens[0].type == JSON_TYPE_ARRAY)  // list of parameter names
    {
        for(int i = 1; i <= tokens[0].num_desc; ++i)
        {
            if(!strncmp(tokens[i].ptr, "count_time", tokens[i].len))
                getDoubleP (SSDetConfig, "count_time", ADAcquireTime);
            else if(!strncmp(tokens[i].ptr, "frame_time", tokens[i].len))
                getDoubleP (SSDetConfig, "frame_time", ADAcquirePeriod);
            else if(!strncmp(tokens[i].ptr, "nimages", tokens[i].len))
                getIntP (SSDetConfig, "nimages", ADNumImages);
            else if(!strncmp(tokens[i].ptr, "photon_energy", tokens[i].len))
                getDoubleP(SSDetConfig, "photon_energy", EigerPhotonEnergy);
            else if(!strncmp(tokens[i].ptr, "beam_center_x", tokens[i].len))
                getDoubleP(SSDetConfig, "beam_center_x", EigerBeamX);
            else if(!strncmp(tokens[i].ptr, "beam_center_y", tokens[i].len))
                getDoubleP(SSDetConfig, "beam_center_y", EigerBeamY);
            else if(!strncmp(tokens[i].ptr, "detector_distance", tokens[i].len))
                getDoubleP(SSDetConfig, "detector_distance", EigerDetDist);
            else if(!strncmp(tokens[i].ptr, "threshold_energy", tokens[i].len))
                getDoubleP(SSDetConfig, "threshold_energy", EigerThreshold);
            else if(!strncmp(tokens[i].ptr, "wavelength", tokens[i].len))
                getDoubleP(SSDetConfig, "wavelength", EigerWavelength);
        }
        callParamCallbacks();
    }
    else
    {
        ERR("unexpected json token type");
        return asynError;
    }

    return status;
}

asynStatus eigerDetector::getString (eigerSys sys, const char *param,
        char *value, size_t len)
{
    return get(sys, param, value, len);
}

asynStatus eigerDetector::getInt (eigerSys sys, const char *param, int *value)
{
    const char *functionName = "getInt";
    asynStatus status;
    char buf[MAX_BUF_SIZE];

    status = get(sys, param, buf, sizeof(buf));
    if(status)
    {
        ERR_ARGS("[param=%s] underlying get failed", param);
        return status;
    }

    int scanned = sscanf(buf, "%d", value);
    if(scanned != 1)
    {
        ERR_ARGS("[param=%s] couldn't parse '%s' as integer", param, buf);
        return asynError;
    }

    return asynSuccess;
}

asynStatus eigerDetector::getDouble (eigerSys sys, const char *param,
        double *value)
{
    const char *functionName = "getDouble";
    asynStatus status;
    char buf[MAX_BUF_SIZE];

    status = get(sys, param, buf, sizeof(buf));
    if(status)
    {
        ERR_ARGS("[param=%s] underlying get failed",  param);
        return status;
    }

    int scanned = sscanf(buf, "%lf", value);
    if(scanned != 1)
    {
        ERR_ARGS("[param=%s] couldn't parse '%s' as double", param, buf);
        return asynError;
    }

    return asynSuccess;
}

asynStatus eigerDetector::getBool (eigerSys sys, const char *param, bool *value)
{
    const char *functionName = "getBool";
    asynStatus status;
    char buf[MAX_BUF_SIZE];

    status = get(sys, param, buf, sizeof(buf));
    if(status)
    {
        ERR_ARGS("[param=%s] underlying get failed", param);
        return status;
    }

    *value = buf[0] == 't';

    return asynSuccess;
}

asynStatus eigerDetector::getStringP (eigerSys sys, const char *param, int dest)
{
    int status;
    char value[MAX_BUF_SIZE];

    status = getString(sys, param, value, sizeof(value)) |
            setStringParam(dest, value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getIntP (eigerSys sys, const char *param, int dest)
{
    int status;
    int value;

    status = getInt(sys, param, &value) | setIntegerParam(dest,value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getDoubleP (eigerSys sys, const char *param, int dest)
{
    int status;
    double value;

    status = getDouble(sys, param, &value) | setDoubleParam(dest, value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getBoolP (eigerSys sys, const char *param, int dest)
{
    int status;
    bool value;

    status = getBool(sys, param, &value) | setIntegerParam(dest, (int)value);
    return (asynStatus)status;
}

asynStatus eigerDetector::putString (eigerSys sys, const char *param,
        const char *value)
{
    const char *functionName = "putString";
    char buf[MAX_BUF_SIZE];
    size_t len = sprintf(buf, "{\"value\": \"%s\"}", value);
    asynStatus status;

    if((status = put(sys, param, buf, len)))
        ERR_ARGS("[param=%s] underlying put failed", param);

    return status;
}

asynStatus eigerDetector::putInt (eigerSys sys, const char *param, int value)
{
    const char *functionName = "putInt";
    char buf[MAX_BUF_SIZE];
    size_t len = sprintf(buf, "{\"value\":%d}", value);
    asynStatus status;

    if((status = put(sys, param, buf, len)))
        ERR_ARGS("[param=%s] underlying put failed", param);

    return status;
}

asynStatus eigerDetector::putBool (eigerSys sys, const char *param, bool value)
{
    const char *functionName = "putBool";
    char buf[MAX_BUF_SIZE];
    size_t len = sprintf(buf, "{\"value\": %s}", value ? "true" : "false");
    asynStatus status;

    if((status = put(sys, param, buf, len)))
        ERR_ARGS("[param=%s] underlying put failed", param);

    return status;
}

asynStatus eigerDetector::putDouble (eigerSys sys, const char *param,
        double value)
{
    const char *functionName = "putDouble";
    char buf[MAX_BUF_SIZE];
    size_t len = sprintf(buf, "{\"value\": %lf}", value);
    asynStatus status;

    if((status = put(sys, param, buf, len)))
        ERR_ARGS("[param=%s] underlying put failed", param);

    return status;
}

asynStatus eigerDetector::command (const char *name, double timeout)
{
    const char *functionName = "command";
    asynStatus status;

    if((status = put(SSCommand, name, NULL, 0, timeout)))
        ERR("underlying put failed");

    return status;
}

asynStatus eigerDetector::getFileSize (const char *remoteFile, size_t *len)
{
    const char *functionName = "getFileSize";
    const char *reqFmt = REQUEST_HEAD;
    const char *url = eigerSysStr[SSData];
    asynStatus status;
    size_t reqSize;
    struct response response;

    size_t retries = GET_FILE_RETRIES;

    reqSize = snprintf(toServer, sizeof(toServer), reqFmt, url, remoteFile);

    while(retries > 0)
    {
        status = doRequest(reqSize, &response);
        if(status)
        {
            ERR_ARGS("[file=%s] HEAD request failed", remoteFile);
            return status;
        }

        if(response.code == 200)
            break;

        if(response.code != 404)
        {
            ERR_ARGS("[file=%s] server returned error code %d", remoteFile,
                    response.code);
            return asynError;
        }

        // Got 404, file is not there yet
        epicsThreadSleep(.01);
        retries -= 1;
    }

    if(!retries)
    {
        ERR_ARGS("[file=%s] server returned error code %d %d times", remoteFile,
                response.code, GET_FILE_RETRIES);
        return asynError;
    }

    *len = response.contentLength;
    return asynSuccess;
}

asynStatus eigerDetector::getFile (const char *remoteFile, char **data,
        size_t *len)
{
    const char *functionName = "getFile";
    const char *reqFmt = REQUEST_GET_PARTIAL;
    const char *url = eigerSysStr[SSData];
    asynStatus status = asynSuccess;
    size_t reqSize;
    struct response response;
    size_t remaining;
    char *dataPtr;

    *len = 0;

    status = getFileSize(remoteFile, &remaining);
    if(status)
    {
        ERR_ARGS("[file=%s] underlying getFileSize failed", remoteFile);
        return status;
    }

    *data = (char*)malloc(remaining);
    if(!*data)
    {
        ERR_ARGS("[file=%s] malloc(%lu) failed", remoteFile, remaining);
        return asynError;
    }

    dataPtr = *data;
    while(remaining)
    {
        reqSize = snprintf(toServer, sizeof(toServer), reqFmt, url, remoteFile,
                *len, *len + CHUNK_SIZE - 1);

        status = doRequest(reqSize, &response);
        if(status)
        {
            ERR_ARGS("[file=%s] partial GET request failed",
                    remoteFile);
            break;
        }

        if(response.code != 206)
        {
            ERR_ARGS("[file=%s] server returned error code %d", remoteFile,
                    response.code);
            break;
        }

        memcpy(dataPtr, response.data, response.size);

        dataPtr += response.size;
        *len += response.size;
        remaining -= response.size;
    }

    if(status)
    {
        free(*data);
        *data = NULL;
    }

    return status;
}

asynStatus eigerDetector::saveFile (const char *file, char *data, size_t len)
{
    const char *functionName = "saveFile";
    asynStatus status = asynSuccess;
    char fullFileName[MAX_FILENAME_LEN];

    setStringParam(NDFileName, file);
    setStringParam(NDFileTemplate, "%s%s");
    createFileName(sizeof(fullFileName), fullFileName);
    setStringParam(NDFullFileName, fullFileName);
    callParamCallbacks();

    FILE *fhandle = fopen(fullFileName, "wb");
    if(!fhandle)
    {
        ERR_ARGS("[file=%s] unable to open file to be written\n[%s]", file,
                fullFileName);
        return asynError;
    }

    size_t written = fwrite(data, 1, len, fhandle);
    if(written < len)
        ERR_ARGS("[file=%s] failed to write to local file (%lu written)", file,
                written);

    fclose(fhandle);
    return status;
}

asynStatus eigerDetector::capture (eigerTriggerMode triggerMode,
        double triggerTimeout)
{
    const char *functionName = "capture";
    asynStatus status;
    asynStatus retStatus = asynSuccess;

    disarmEvent.tryWait();  // Clear any previously uncaught disarm event

    setStringParam(ADStatusMessage, "Arming the detector (takes a while)");
    callParamCallbacks();

    // Arm the detector
    if((status = command("arm", DEF_TIMEOUT_ARM)))
    {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: failed to arm the detector\n",
                driverName, functionName);

        retStatus = status;
        setStringParam(ADStatusMessage, "Failed to arm the detector");
        goto end;
    }

    // Set armed flag
    setIntegerParam(EigerArmed, 1);
    setStringParam(ADStatusMessage, "Detector armed");
    callParamCallbacks();

    // Actually acquire the image(s)
    if(triggerMode == TMInternalSeries)
    {
        if((status = command("trigger", triggerTimeout)))
        {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: failed to trigger the detector\n",
                    driverName, functionName);

            retStatus = status;
            setStringParam(ADStatusMessage, "Failed to trigger the detector");
            // continue to disarm
        }

        // Image(s) acquired or aborted. Disarm the detector
        if((status = command("disarm")))
        {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: failed to disarm the detector\n",
                    driverName, functionName);

            retStatus = status;
            setStringParam(ADStatusMessage, "Failed to disarm the detector");
            goto end;
        }
    }
    else
    {
        disarmEvent.wait();
    }

    setIntegerParam(EigerArmed, 0);

end:
    callParamCallbacks();
    return retStatus;
}

asynStatus eigerDetector::downloadAndPublish (void)
{
    const char *functionName = "downloadAndPublish";
    asynStatus status = asynSuccess;
    int saveFiles, numImages, sequenceId, numImagesPerFile, nrStart, nFiles;
    char pattern[MAX_BUF_SIZE];
    char *prefix, *suffix;

    getIntegerParam(EigerSaveFiles,      &saveFiles);
    getIntegerParam(ADNumImages,         &numImages);
    getIntegerParam(EigerSequenceId,     &sequenceId);
    getIntegerParam(EigerFWNImgsPerFile, &numImagesPerFile);
    getIntegerParam(EigerFWImageNrStart, &nrStart);
    getStringParam (EigerFWNamePattern,  sizeof(pattern), pattern);

    // Compute prefix and suffix to file name
    prefix = pattern;
    suffix = strstr(pattern, ID_STR);
    *suffix = '\0';
    suffix += ID_LEN;

    setIntegerParam(ADStatus, ADStatusReadout);
    setStringParam(ADStatusMessage, "Downloading data files");
    callParamCallbacks();

    // Wait for file to exist
    // TODO: Is this the best way?
    char buf[MAX_BUF_SIZE];
    do
    {
        getString(SSFWStatus, "state", buf, sizeof(buf));
    }while(buf[0] == 'a');

    // Calculate number of files (master + data files)
    nFiles = (int) ceil(((double)numImages)/((double)numImagesPerFile)) + 1;

    for(int i = 0; i < nFiles; ++i)
    {
        bool isMaster = i == 0;

        // Only download master if saving files locally
        if(isMaster && !saveFiles)
            continue;

        // Build file names accordingly, first one is the master file
        char fileName[MAX_BUF_SIZE];
        if(isMaster)
            sprintf(fileName, "%s%d%s_master.h5", prefix, sequenceId,
                    suffix);
        else
            sprintf(fileName, "%s%d%s_data_%06d.h5", prefix, sequenceId,
                    suffix, i-1+nrStart);

        // Download file into memory
        char *data = NULL;
        size_t dataLen;

        status = getFile(fileName, &data, &dataLen);
        if(status)
        {
            ERR_ARGS("underlying getFile(%s) failed", fileName);
            break;
        }

        // Save file to disk
        if(saveFiles)
        {
            status = saveFile(fileName, data, dataLen);
            if(status)
            {
                ERR_ARGS("underlying saveFile(%s) failed", fileName);
                free(data);
                break;
            }
        }

        // Parse file into NDArray
        if(!isMaster)
        {
            status = parseH5File(data, dataLen);
            if(status)
            {
                ERR_ARGS("underlying parseH5File(%s) failed", fileName);
                free(data);
                break;
            }
        }

        free(data);
    }

    return status;
}

asynStatus eigerDetector::parseH5File (char *buf, size_t bufLen)
{
    const char *functionName = "parseH5File";
    asynStatus status = asynSuccess;

    int imageCounter, arrayCallbacks;
    hid_t fId, dId, dSpace, dType, mSpace;
    hsize_t dims[3], count[3], offset[3] = {0,0,0};
    herr_t err;
    size_t nImages, width, height;

    size_t ndDims[2];
    NDDataType_t ndType;

    epicsTimeStamp startTime;

    unsigned flags = H5LT_FILE_IMAGE_DONT_COPY | H5LT_FILE_IMAGE_DONT_RELEASE;

    // Open h5 file from memory
    fId = H5LTopen_file_image(buf, bufLen, flags);
    if(fId < 0)
    {
        ERR("unable to open memory as file");
        goto end;
    }

    // Access dataset 'data'
    dId = H5Dopen2(fId, "/entry/data/data", H5P_DEFAULT);
    if(dId < 0)
    {
        ERR("unable to open '/entry/data/data'. Will try '/entry/data'");

        dId = H5Dopen2(fId, "/entry/data", H5P_DEFAULT);
        if(dId < 0)
        {
            ERR("unable to open '/entry/data' dataset");
            goto closeFile;
        }
    }

    // Get dataset dimensions (assume 3 dimensions)
    err = H5LTget_dataset_info(dId, ".", dims, NULL, NULL);
    if(err)
    {
        ERR("couldn't read dataset info");
        goto closeDataset;
    }

    nImages = dims[0];
    height  = dims[1];
    width   = dims[2];

    ndDims[0] = width;
    ndDims[1] = height;

    count[0] = 1;
    count[1] = height;
    count[2] = width;

    // Get dataset type
    dType = H5Dget_type(dId);
    if(dType < 0)
    {
        ERR("couldn't get dataset type");
        goto closeDataset;
    }

    // Parse dataset type
    if(H5Tequal(dType, H5T_NATIVE_UINT32) > 0)
        ndType = NDUInt32;
    else if(H5Tequal(dType, H5T_NATIVE_UINT16) > 0)
        ndType = NDUInt16;
    else
    {
        ERR("invalid data type");
        goto closeDataType;
    }

    // Get dataspace
    dSpace = H5Dget_space(dId);
    if(dSpace < 0)
    {
        ERR("couldn't get dataspace");
        goto closeDataType;
    }

    // Create memspace
    mSpace = H5Screate_simple(3, count, NULL);
    if(mSpace < 0)
    {
        ERR("failed to create memSpace");
        goto closeMemSpace;
    }

    getIntegerParam(NDArrayCounter, &imageCounter);
    for(offset[0] = 0; offset[0] < nImages; ++offset[0])
    {
        NDArray *pImage;

        pImage = pNDArrayPool->alloc(2, ndDims, ndType, 0, NULL);
        if(!pImage)
        {
            ERR("couldn't allocate NDArray");
            break;
        }

        // Select the hyperslab
        err = H5Sselect_hyperslab(dSpace, H5S_SELECT_SET, offset, NULL,
                count, NULL);
        if(err < 0)
        {
            ERR("couldn't select hyperslab");
            pImage->release();
            break;
        }

        // and finally read the image
        err = H5Dread(dId, dType, mSpace, dSpace, H5P_DEFAULT, pImage->pData);
        if(err < 0)
        {
            ERR("couldn't read image");
            pImage->release();
            break;
        }

        // Put the frame number and time stamp into the buffer
        pImage->uniqueId = imageCounter;
        epicsTimeGetCurrent(&startTime);
        pImage->timeStamp =  startTime.secPastEpoch + startTime.nsec / 1.e9;
        updateTimeStamp(&pImage->epicsTS);

        // Get any attributes that have been defined for this driver
        this->getAttributes(pImage->pAttributeList);

        // Call the NDArray callback
        getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
        if (arrayCallbacks)
        {
            this->unlock();
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: calling NDArray callback\n",
                    driverName, functionName);

            doCallbacksGenericPointer(pImage, NDArrayData, 0);
            this->lock();
        }

        setIntegerParam(NDArrayCounter, ++imageCounter);
        callParamCallbacks();

        pImage->release();
    }

closeMemSpace:
    H5Sclose(mSpace);
closeDataType:
    H5Tclose(dType);
closeDataset:
    H5Dclose(dId);
closeFile:
    H5Fclose(fId);
end:
    return status;
}

/** This function is called periodically read the detector status (temperature,
  * humidity, etc.). It should not be called if we are acquiring data, to avoid
  * polling server when taking data.*/
asynStatus eigerDetector::eigerStatus (void)
{
    int status;
    double temp = 0.0;
    double humid = 0.0;
    char link[4][MAX_BUF_SIZE];

    // Read temperature and humidity
    status  = getDouble(SSDetStatus, "board_000/th0_temp",     &temp);
    status |= getDouble(SSDetStatus, "board_000/th0_humidity", &humid);
    status |= getString(SSDetStatus, "link0", link[0], sizeof(link[0]));
    status |= getString(SSDetStatus, "link1", link[1], sizeof(link[1]));
    status |= getString(SSDetStatus, "link2", link[2], sizeof(link[2]));
    status |= getString(SSDetStatus, "link3", link[3], sizeof(link[3]));

    if(!status)
    {
        setDoubleParam(ADTemperature, temp);
        setDoubleParam(EigerThTemp0,  temp);
        setDoubleParam(EigerThHumid0, humid);
        setIntegerParam(EigerLink0, !strcmp(link[0], "up"));
        setIntegerParam(EigerLink1, !strcmp(link[1], "up"));
        setIntegerParam(EigerLink2, !strcmp(link[2], "up"));
        setIntegerParam(EigerLink3, !strcmp(link[3], "up"));
    }
    else
        setIntegerParam(ADStatus, ADStatusError);

    // Other temperatures/humidities available, do we want them?
    // "board_000/th1_temp"   "board_000/th1_humidity"
    // "module_000/temp"      "module_000/humidity"
    // "module_001/temp"      "module_001/humidity"
    // "module_002/temp"      "module_002/humidity"
    // "module_003/temp"      "module_003/humidity"

    callParamCallbacks();
    return (asynStatus)status;
}

extern "C" int eigerDetectorConfig(const char *portName, const char *serverPort,
        int maxBuffers, size_t maxMemory, int priority, int stackSize)
{
    new eigerDetector(portName, serverPort, maxBuffers, maxMemory, priority,
            stackSize);
    return(asynSuccess);
}

/* Code for iocsh registration */
static const iocshArg eigerDetectorConfigArg0 = {"Port name", iocshArgString};
static const iocshArg eigerDetectorConfigArg1 = {"Server port name",
    iocshArgString};
static const iocshArg eigerDetectorConfigArg2 = {"maxBuffers", iocshArgInt};
static const iocshArg eigerDetectorConfigArg3 = {"maxMemory", iocshArgInt};
static const iocshArg eigerDetectorConfigArg4 = {"priority", iocshArgInt};
static const iocshArg eigerDetectorConfigArg5 = {"stackSize", iocshArgInt};
static const iocshArg * const eigerDetectorConfigArgs[] = {
    &eigerDetectorConfigArg0, &eigerDetectorConfigArg1,
    &eigerDetectorConfigArg2, &eigerDetectorConfigArg3,
    &eigerDetectorConfigArg4, &eigerDetectorConfigArg5};

static const iocshFuncDef configeigerDetector = {"eigerDetectorConfig", 6,
    eigerDetectorConfigArgs};

static void configeigerDetectorCallFunc(const iocshArgBuf *args)
{
    eigerDetectorConfig(args[0].sval, args[1].sval, args[2].ival, args[3].ival,
            args[4].ival, args[5].ival);
}

static void eigerDetectorRegister(void)
{
    iocshRegister(&configeigerDetector, configeigerDetectorCallFunc);
}

extern "C" {
    epicsExportRegistrar(eigerDetectorRegister);
}

