/*
 * This is a driver for the Eiger pixel array detector.
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
#include <epicsMessageQueue.h>
#include <iocsh.h>
#include <math.h>

#include <hdf5.h>
#include <hdf5_hl.h>

#include "ADDriver.h"
#include "eigerDetector.h"
#include "restApi.h"
#include "streamApi.h"

#define MAX_BUF_SIZE            256
#define DEFAULT_NR_START        1
#define DEFAULT_QUEUE_CAPACITY  2
#define MONITOR_MIN_PERIOD      0.1

// Error message formatters
#define ERR(msg) asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s::%s: %s\n", \
    driverName, functionName, msg)

#define ERR_ARGS(fmt,...) asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, \
    "%s::%s: "fmt"\n", driverName, functionName, __VA_ARGS__);

// Flow message formatters
#define FLOW(msg) asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s::%s: %s\n", \
    driverName, functionName, msg)

#define FLOW_ARGS(fmt,...) asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, \
    "%s::%s: "fmt"\n", driverName, functionName, __VA_ARGS__);

typedef struct
{
    char pattern[MAX_BUF_SIZE];
    int sequenceId;
    size_t nDataFiles;
    bool saveFiles, parseFiles, removeFiles;
}acquisition_t;

typedef struct
{
    char name[MAX_BUF_SIZE];
    char *data;
    size_t len;
    bool save, parse, remove;
    size_t refCount;
}file_t;

enum data_source
{
    SOURCE_NONE,
    SOURCE_FILEWRITER,
    SOURCE_STREAM,
};

typedef struct
{
    size_t id;
    eigerDetector *detector;
    epicsMessageQueue *jobQueue, *doneQueue;
    epicsEvent *cbEvent, *nextCbEvent;
}stream_worker_t;

typedef struct
{
    void *data;
    size_t compressedSize, uncompressedSize;
    size_t dims[2];
    NDDataType_t type;
}stream_job_t;

static const char *driverName = "eigerDetector";

static inline size_t numDataFiles (int nTriggers, int nImages, int nImagesPerFile)
{
    return (size_t) ceil(((double)(nImages*nTriggers))/((double)nImagesPerFile));
}

static void controlTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->controlTask();
}

static void pollTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->pollTask();
}

static void downloadTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->downloadTask();
}

static void parseTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->parseTask();
}

static void saveTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->saveTask();
}

static void reapTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->reapTask();
}

static void monitorTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->monitorTask();
}

static void streamTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->streamTask();
}

/* Constructor for Eiger driver; most parameters are simply passed to
 * ADDriver::ADDriver.
 * After calling the base class constructor this method creates a thread to
 * collect the detector data, and sets reasonable default values for the
 * parameters defined in this class, asynNDArrayDriver, and ADDriver.
 * \param[in] portName The name of the asyn port driver to be created.
 * \param[in] serverHostname The IP or url of the detector webserver.
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
eigerDetector::eigerDetector (const char *portName, const char *serverHostname,
        int maxBuffers, size_t maxMemory, int priority,
        int stackSize)

    : ADDriver(portName, 1, NUM_EIGER_PARAMS, maxBuffers, maxMemory,
               0, 0,             /* No interfaces beyond ADDriver.cpp */
               ASYN_CANBLOCK |   /* ASYN_CANBLOCK=1 */
               ASYN_MULTIDEVICE, /* ASYN_MULTIDEVICE=1 */
               1,                /* autoConnect=1 */
               priority, stackSize),
    mApi(serverHostname),
    mStartEvent(), mStopEvent(), mTriggerEvent(), mPollDoneEvent(),
    mPollQueue(1, sizeof(acquisition_t)),
    mDownloadQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mParseQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mSaveQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mReapQueue(DEFAULT_QUEUE_CAPACITY*2, sizeof(file_t *))
{
    const char *functionName = "eigerDetector";

    strncpy(mHostname, serverHostname, sizeof(mHostname));

    // Initialize sockets
    RestAPI::init();

    // Data Source Parameter
    createParam(EigerDataSourceString,    asynParamInt32, &EigerDataSource);

    // FileWriter Parameters
    createParam(EigerFWEnableString,      asynParamInt32, &EigerFWEnable);
    createParam(EigerFWClearString,       asynParamInt32, &EigerFWClear);
    createParam(EigerFWCompressionString, asynParamInt32, &EigerFWCompression);
    createParam(EigerFWNamePatternString, asynParamOctet, &EigerFWNamePattern);
    createParam(EigerFWNImgsPerFileString,asynParamInt32, &EigerFWNImgsPerFile);
    createParam(EigerFWAutoRemoveString,  asynParamInt32, &EigerFWAutoRemove);
    createParam(EigerFWFreeString,        asynParamInt32, &EigerFWFree);

    // Acquisition Metadata Parameters
    createParam(EigerBeamXString,         asynParamFloat64, &EigerBeamX);
    createParam(EigerBeamYString,         asynParamFloat64, &EigerBeamY);
    createParam(EigerDetDistString,       asynParamFloat64, &EigerDetDist);
    createParam(EigerWavelengthString,    asynParamFloat64, &EigerWavelength);

    // Acquisition Parameters
    createParam(EigerFlatfieldString,     asynParamInt32,   &EigerFlatfield);
    createParam(EigerPhotonEnergyString,  asynParamFloat64, &EigerPhotonEnergy);
    createParam(EigerThresholdString,     asynParamFloat64, &EigerThreshold);
    createParam(EigerTriggerString,       asynParamInt32,   &EigerTrigger);
    createParam(EigerTriggerExpString,    asynParamFloat64, &EigerTriggerExp);
    createParam(EigerNTriggersString,     asynParamInt32,   &EigerNTriggers);
    createParam(EigerManualTriggerString, asynParamInt32,   &EigerManualTrigger);

    // Detector Info Parameters
    createParam(EigerSWVersionString,     asynParamOctet,   &EigerSWVersion);
    createParam(EigerSerialNumberString,  asynParamOctet,   &EigerSerialNumber);

    // Detector Status Parameters
    createParam(EigerStateString,         asynParamOctet,   &EigerState);
    createParam(EigerErrorString,         asynParamOctet,   &EigerError);
    createParam(EigerThTemp0String,       asynParamFloat64, &EigerThTemp0);
    createParam(EigerThHumid0String,      asynParamFloat64, &EigerThHumid0);
    createParam(EigerLink0String,         asynParamInt32,   &EigerLink0);
    createParam(EigerLink1String,         asynParamInt32,   &EigerLink1);
    createParam(EigerLink2String,         asynParamInt32,   &EigerLink2);
    createParam(EigerLink3String,         asynParamInt32,   &EigerLink3);
    createParam(EigerDCUBufFreeString,    asynParamFloat64, &EigerDCUBufFree);

    // Other Parameters
    createParam(EigerArmedString,         asynParamInt32,   &EigerArmed);
    createParam(EigerSaveFilesString,     asynParamInt32,   &EigerSaveFiles);
    createParam(EigerSequenceIdString,    asynParamInt32,   &EigerSequenceId);
    createParam(EigerPendingFilesString,  asynParamInt32,   &EigerPendingFiles);

    // Monitor API Parameters
    createParam(EigerMonitorEnableString, asynParamInt32,   &EigerMonitorEnable);
    createParam(EigerMonitorPeriodString, asynParamFloat64, &EigerMonitorPeriod);

    // Stream API Parameters
    createParam(EigerStreamEnableString,  asynParamInt32, &EigerStreamEnable);
    createParam(EigerStreamDroppedString, asynParamInt32, &EigerStreamDropped);

    // Test if the detector is initialized
    if(mApi.getString(SSDetConfig, "description", NULL, 0))
    {
        ERR("Eiger seems to be uninitialized\nInitializing... (may take a while)");

        if(mApi.initialize())
        {
            ERR("Eiger FAILED TO INITIALIZE");
            return;
        }

        FLOW("Eiger initialized");
    }

    // Set default parameters
    if(initParams())
    {
        ERR("unable to set detector parameters");
        return;
    }

    // Read status once at startup
    eigerStatus();

    // Task creation
    int status = asynSuccess;
    status = (epicsThreadCreate("eigerControlTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)controlTaskC, this) == NULL);

    status |= (epicsThreadCreate("eigerPollTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)pollTaskC, this) == NULL);

    status |= (epicsThreadCreate("eigerDownloadTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)downloadTaskC, this) == NULL);

    status |= (epicsThreadCreate("eigerParseTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)parseTaskC, this) == NULL);

    status |= (epicsThreadCreate("eigerSaveTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)saveTaskC, this) == NULL);

    status |= (epicsThreadCreate("eigerReapTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)reapTaskC, this) == NULL);

    status |= (epicsThreadCreate("eigerMonitorTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)monitorTaskC, this) == NULL);

    status |= (epicsThreadCreate("eigerStreamTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)streamTaskC, this) == NULL);

    if(status)
        ERR("epicsThreadCreate failure for some task");
}

/* Called when asyn clients call pasynInt32->write().
 * This function performs actions for some parameters, including ADAcquire,
 * ADTriggerMode, etc.
 * For all parameters it sets the value in the parameter library and calls any
 * registered callbacks..
 * \param[in] pasynUser pasynUser structure that encodes the reason and address.
 * \param[in] value Value to write.
 */
asynStatus eigerDetector::writeInt32 (asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeInt32";

    int adStatus, armed;
    getIntegerParam(ADStatus, &adStatus);
    getIntegerParam(EigerArmed, &armed);

    if(function == ADAcquire)
    {
        // Start
        if(value && adStatus != ADStatusAcquire)
        {
            setIntegerParam(ADStatus, ADStatusAcquire);
            mStartEvent.signal();
        }
        // Stop
        else if (!value && adStatus == ADStatusAcquire)
        {
            unlock();
            mApi.abort();
            lock();
            setIntegerParam(ADStatus, ADStatusAborted);
            mStopEvent.signal();
        }
    }
    else if (function == EigerFWClear)
    {
        status = putInt(SSFWConfig, "clear", 1);
        getIntP(SSFWStatus, "buffer_free", EigerFWFree);
    }
    else if (function == EigerFWCompression)
        status = putBool(SSFWConfig, "compression_enabled", (bool)value);
    else if (function == EigerFWNImgsPerFile)
        status = putInt(SSFWConfig, "nimages_per_file", value);
    else if (function == EigerFlatfield)
        status = putBool(SSDetConfig, "flatfield_correction_applied", (bool)value);
    else if (function == EigerNTriggers)
        status = putInt(SSDetConfig, "ntrigger", value);
    else if (function == ADTriggerMode)
        status = putString(SSDetConfig, "trigger_mode", RestAPI::triggerModeStr[value]);
    else if (function == ADNumImages)
        status = putInt(SSDetConfig, "nimages", value);
    else if (function == ADReadStatus)
        status = eigerStatus();
    else if (function == EigerTrigger)
        mTriggerEvent.signal();
    else if (function == EigerFWEnable)
        status = putString(SSFWConfig, "mode", value ? "enabled" : "disabled");
    else if (function == EigerStreamEnable)
        status = putString(SSStreamConfig, "mode", value ? "enabled" : "disabled");
    else if (function == EigerMonitorEnable)
        status = putString(SSMonConfig, "mode", value ? "enabled" : "disabled");
    else if(function < FIRST_EIGER_PARAM)
        status = ADDriver::writeInt32(pasynUser, value);

    if(status)
    {
        ERR_ARGS("error status=%d function=%d, value=%d", status, function, value);
        return status;
    }

    status = setIntegerParam(function, value);
    callParamCallbacks();

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

/* Called when asyn clients call pasynFloat64->write().
 * This function performs actions for some parameters, including ADAcquireTime,
 * ADAcquirePeriod, etc.
 * For all parameters it sets the value in the parameter library and calls any
 * registered callbacks..
 * \param[in] pasynUser pasynUser structure that encodes the reason and
 *            address.
 * \param[in] value Value to write.
 */
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
    else if (function == EigerMonitorPeriod)
        value = value < MONITOR_MIN_PERIOD ? MONITOR_MIN_PERIOD : value;
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

        // Do callbacks so higher layers see any changes
        setDoubleParam(function, value);
        callParamCallbacks();
    }
    return status;
}

/** Called when asyn clients call pasynOctet->write().
  * This function performs actions for EigerFWNamePattern
  * For all parameters it sets the value in the parameter library and calls any
  * registered callbacks.
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
        status = putString(SSFWConfig, "name_pattern", value);
    else if (function < FIRST_EIGER_PARAM)
        status = ADDriver::writeOctet(pasynUser, value, nChars, nActual);

    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "%s:%s: status=%d, function=%d, value=%s",
                  driverName, functionName, status, function, value);
    else
    {
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s:%s: function=%d, value=%s\n",
              driverName, functionName, function, value);

        setStringParam(function, value);
        callParamCallbacks();
    }

    *nActual = nChars;
    return status;
}

/* Report status of the driver.
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

    // Invoke the base class method
    ADDriver::report(fp, details);
}

/*
 * This thread controls the data acquisition by the detector
 */
void eigerDetector::controlTask (void)
{
    RestAPI api(mHostname);
    const char *functionName = "controlTask";

    int status = asynSuccess;
    int dataSource, fwEnable, streamEnable;
    int adStatus, manualTrigger;
    int sequenceId, saveFiles, numImages, numTriggers, triggerMode;
    int numImagesPerFile, removeFiles;
    double acquirePeriod, triggerTimeout = 0.0, triggerExposure;

    lock();

    for(;;)
    {
        // Clear uncaught events
        mStopEvent.tryWait();
        mTriggerEvent.tryWait();
        mPollDoneEvent.tryWait();
        mStreamEvent.tryWait();

        // Wait for start event
        getIntegerParam(ADStatus, &adStatus);
        if(adStatus == ADStatusIdle)
            setStringParam(ADStatusMessage, "Waiting for acquire command");
        callParamCallbacks();

        unlock();
        mStartEvent.wait();
        lock();

        // Latch parameters
        getIntegerParam(EigerDataSource,     &dataSource);
        getIntegerParam(EigerFWEnable,       &fwEnable);
        getIntegerParam(EigerStreamEnable,   &streamEnable);
        getIntegerParam(EigerSaveFiles,      &saveFiles);
        getIntegerParam(EigerFWNImgsPerFile, &numImagesPerFile);
        getDoubleParam (ADAcquirePeriod,     &acquirePeriod);
        getIntegerParam(ADNumImages,         &numImages);
        getIntegerParam(EigerNTriggers,      &numTriggers);
        getIntegerParam(ADTriggerMode,       &triggerMode);
        getIntegerParam(EigerManualTrigger,  &manualTrigger);
        getIntegerParam(EigerFWAutoRemove,   &removeFiles);

        if(dataSource == SOURCE_FILEWRITER && !fwEnable)
        {
            setIntegerParam(ADAcquire, 0);
            setIntegerParam(ADStatus, ADStatusError);
            setStringParam(ADStatusMessage, "FileWriter API is disabled");
            continue;
        }

        if(dataSource == SOURCE_STREAM && !streamEnable)
        {
            setIntegerParam(ADAcquire, 0);
            setIntegerParam(ADStatus, ADStatusError);
            setStringParam(ADStatusMessage, "Stream API is disabled");
            continue;
        }

        // If saving files, check if the File Path is valid
        if(fwEnable && saveFiles)
        {
            int filePathExists;
            checkPath();
            getIntegerParam(NDFilePathExists, &filePathExists);

            if(!filePathExists)
            {
                ERR("invalid local file path");
                setIntegerParam(ADAcquire, 0);
                setIntegerParam(ADStatus, ADStatusError);
                setStringParam(ADStatusMessage, "Invalid file path");
                continue;
            }
        }

        // Arm the detector
        setStringParam(ADStatusMessage, "Arming...");
        callParamCallbacks();

        unlock();
        status = api.arm(&sequenceId);
        lock();

        if(status)
        {
            ERR("Failed to arm the detector");
            setIntegerParam(ADAcquire, 0);
            setIntegerParam(ADStatus, ADStatusError);
            setStringParam(ADStatusMessage, "Failed to arm the detector");
            continue;
        }

        // Set status parameters
        setIntegerParam(ADStatus,        ADStatusAcquire);
        setStringParam (ADStatusMessage, "Detector armed");
        setIntegerParam(EigerSequenceId, sequenceId);
        setIntegerParam(EigerArmed, 1);
        callParamCallbacks();

        switch(triggerMode)
        {
        case TMInternalSeries:
            triggerTimeout = acquirePeriod*numImages + 10.0;
            break;

        case TMInternalEnable:
        case TMExternalEnable:
            numImages = 1;
            break;
        }

        bool waitPoll = false, waitStream = false;

        // Start FileWriter thread
        if(dataSource == SOURCE_FILEWRITER || (fwEnable && saveFiles))
        {
            acquisition_t acq;
            getStringParam(EigerFWNamePattern, sizeof(acq.pattern), acq.pattern);
            acq.sequenceId  = sequenceId;
            acq.nDataFiles  = numDataFiles(numTriggers, numImages, numImagesPerFile);
            acq.saveFiles   = saveFiles;
            acq.parseFiles  = dataSource == SOURCE_FILEWRITER;
            acq.removeFiles = removeFiles;
            mPollQueue.send(&acq, sizeof(acq));
            waitPoll = true;
        }

        // Start Stream thread
        if(dataSource == SOURCE_STREAM)
        {
            mStreamEvent.signal();
            waitStream = true;
        }

        // Open shutter
        setShutter(1);

        // Trigger
        if(manualTrigger)
            setStringParam(ADStatusMessage, "Waiting for triggers...");
        else
            setStringParam(ADStatusMessage, "Triggering...");
        callParamCallbacks();

        if(triggerMode == TMInternalSeries || triggerMode == TMInternalEnable)
        {
            getIntegerParam(ADStatus, &adStatus);

            int triggers = 0;
            while(adStatus == ADStatusAcquire && triggers < numTriggers)
            {
                bool doTrigger = true;

                if(manualTrigger)
                {
                    unlock();
                    doTrigger = mTriggerEvent.wait(0.1);
                    lock();

                    getDoubleParam(EigerTriggerExp, &triggerExposure);
                    triggerTimeout = triggerExposure + 1.0;
                }

                if(doTrigger)
                {
                    unlock();
                    status = api.trigger(triggerTimeout, triggerExposure);
                    lock();
                    ++triggers;
                }

                getIntegerParam(ADStatus, &adStatus);
            }
        }
        else // triggerMode == TMExternalSeries || triggerMode == TMExternalEnable
            mStopEvent.wait();

        // Close shutter
        setShutter(0);

        // All triggers issued, disarm the detector and wait for pollTask
        unlock();
        status = api.disarm();
        lock();

        setIntegerParam(EigerArmed, 0);
        setStringParam(ADStatusMessage, "Waiting for files to be processed...");
        callParamCallbacks();

        unlock();
        if(waitPoll)
            mPollDoneEvent.wait();
        if(waitStream)
            mStreamDoneEvent.wait();
        lock();

        getIntegerParam(ADStatus, &adStatus);
        if(adStatus == ADStatusAcquire)
            setIntegerParam(ADStatus, ADStatusIdle);
        else if(adStatus == ADStatusAborted)
            setStringParam(ADStatusMessage, "Acquisition aborted");

        setIntegerParam(ADAcquire, 0);
        callParamCallbacks();
    }
}

void eigerDetector::pollTask (void)
{
    RestAPI api(mHostname);
    acquisition_t acquisition;
    int adStatus, pendingFiles;
    size_t totalFiles, i;
    file_t *files;

    for(;;)
    {
        mPollQueue.receive(&acquisition, sizeof(acquisition));

        // Generate files list
        totalFiles = acquisition.nDataFiles + 1;
        files = (file_t*) calloc(totalFiles, sizeof(*files));

        for(i = 0; i < totalFiles; ++i)
        {
            bool isMaster = i == 0;

            files[i].save     = acquisition.saveFiles;
            files[i].parse    = isMaster ? false : acquisition.parseFiles;
            files[i].refCount = files[i].save + files[i].parse;
            files[i].remove   = acquisition.removeFiles;

            if(isMaster)
                RestAPI::buildMasterName(acquisition.pattern, acquisition.sequenceId,
                        files[i].name, sizeof(files[i].name));
            else
                RestAPI::buildDataName(i-1+DEFAULT_NR_START, acquisition.pattern,
                        acquisition.sequenceId, files[i].name,
                        sizeof(files[i].name));
        }

        // While acquiring, wait and download every file on the list
        i = 0;
        do
        {
            file_t *curFile = &files[i];

            if(!api.waitFile(curFile->name, 1.0))
            {
                if(curFile->save || curFile->parse)
                {
                    mDownloadQueue.send(&curFile, sizeof(curFile));

                    lock();
                    getIntegerParam(EigerPendingFiles, &pendingFiles);
                    setIntegerParam(EigerPendingFiles, pendingFiles+1);
                    unlock();
                }
                else if(curFile->remove)
                    api.deleteFile(curFile->name);
                ++i;
            }

            lock();
            getIntegerParam(ADStatus, &adStatus);
            unlock();
        }while(adStatus == ADStatusAcquire && i < totalFiles);

        // Not acquiring anymore, wait for all pending files to be reaped
        do
        {
            lock();
            getIntegerParam(EigerPendingFiles, &pendingFiles);
            unlock();

            epicsThreadSleep(0.1);
        }while(pendingFiles);

        // All pending files were processed and reaped
        free(files);
        mPollDoneEvent.signal();
    }
}

void eigerDetector::downloadTask (void)
{
    RestAPI api(mHostname);
    const char *functionName = "downloadTask";
    file_t *file;

    for(;;)
    {
        mDownloadQueue.receive(&file, sizeof(file_t *));

        FLOW_ARGS("file=%s", file->name);

        file->refCount = file->parse + file->save;

        // Download the file
        if(api.getFile(file->name, &file->data, &file->len))
        {
            ERR_ARGS("underlying getFile(%s) failed", file->name);
            mReapQueue.send(&file, sizeof(file));
        }
        else
        {
            if(file->parse)
                mParseQueue.send(&file, sizeof(file_t *));

            if(file->save)
                mSaveQueue.send(&file, sizeof(file_t *));

            if(file->remove)
                api.deleteFile(file->name);
            else
                getIntP(SSFWStatus, "buffer_free", EigerFWFree);
        }
    }
}

void eigerDetector::parseTask (void)
{
    const char *functionName = "parseTask";
    file_t *file;

    for(;;)
    {
        mParseQueue.receive(&file, sizeof(file_t *));

        FLOW_ARGS("file=%s", file->name);

        if(parseH5File(file->data, file->len))
        {
            ERR_ARGS("underlying parseH5File(%s) failed", file->name);
        }

        mReapQueue.send(&file, sizeof(file));
    }
}

void eigerDetector::saveTask (void)
{
    const char *functionName = "saveTask";
    char fullFileName[MAX_FILENAME_LEN];
    file_t *file;

    for(;;)
    {
        FILE *fhandle = NULL;
        size_t written = 0;

        mSaveQueue.receive(&file, sizeof(file_t *));

        FLOW_ARGS("file=%s", file->name);

        lock();
        setStringParam(NDFileName, file->name);
        setStringParam(NDFileTemplate, "%s%s");
        createFileName(sizeof(fullFileName), fullFileName);
        setStringParam(NDFullFileName, fullFileName);
        callParamCallbacks();
        unlock();

        fhandle = fopen(fullFileName, "wb");
        if(!fhandle)
        {
            ERR_ARGS("[file=%s] unable to open file to be written\n[%s]",
                    file->name, fullFileName);
            goto reap;
        }

        written = fwrite(file->data, 1, file->len, fhandle);
        if(written < file->len)
            ERR_ARGS("[file=%s] failed to write to local file (%lu written)",
                    file->name, written);
        fclose(fhandle);

reap:
        mReapQueue.send(&file, sizeof(file));
    }
}

void eigerDetector::reapTask (void)
{
    const char *functionName = "reapTask";
    file_t *file;
    int pendingFiles;

    for(;;)
    {
        mReapQueue.receive(&file, sizeof(file));

        FLOW_ARGS("file=%s refCount=%lu", file->name, file->refCount)

        if(! --file->refCount)
        {
            if(file->data)
            {
                free(file->data);
                file->data = NULL;
                FLOW_ARGS("file=%s reaped", file->name);
            }

            lock();
            getIntegerParam(EigerPendingFiles, &pendingFiles);
            setIntegerParam(EigerPendingFiles, pendingFiles-1);
            unlock();
        }
    }
}

void eigerDetector::monitorTask (void)
{
    RestAPI eiger(mHostname);
    const char *functionName = "monitorTask";

    for(;;)
    {
        int enabled;
        double period;

        lock();
        getIntegerParam(EigerMonitorEnable, &enabled);
        getDoubleParam(EigerMonitorPeriod, &period);
        unlock();

        if(enabled)
        {
            char *buf = NULL;
            size_t bufSize;

            if(!eiger.getMonitorImage(&buf, &bufSize))
            {
                if(parseTiffFile(buf, bufSize))
                    ERR("couldn't parse file");

                free(buf);
            }
        }

        epicsThreadSleep(period);
    }
}

void eigerDetector::streamTask (void)
{
    const char *functionName = "streamTask";
    for(;;)
    {
        asynStatus status = asynSuccess;
        mStreamEvent.wait();

        StreamAPI api(mHostname);

        stream_header_t header;
        if(api.getHeader(&header))
        {
            ERR("failed to get header packet");
            status = asynError;
            goto end;
        }

        for(;;)
        {
            stream_frame_t frame = {};
            if(api.getFrame(&frame))
            {
                ERR("failed to get frame packet");
                status = asynError;
                goto end;
            }

            if(frame.end)
                break;

            NDArray *pArray;
            size_t *dims = frame.shape;
            NDDataType_t type = frame.type == stream_frame_t::UINT16 ? NDUInt16 : NDUInt32;

            if(!(pArray = pNDArrayPool->alloc(2, dims, type, 0, NULL)))
            {
                ERR_ARGS("failed to allocate NDArray for frame %lu", frame.frame);
                free(frame.data);
                continue;
            }

            StreamAPI::uncompress(&frame, (char*)pArray->pData);
            free(frame.data);

            int imageCounter, arrayCallbacks;
            lock();
            getIntegerParam(NDArrayCounter, &imageCounter);
            getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
            unlock();

            // Put the frame number and timestamp into the buffer
            pArray->uniqueId = imageCounter;

            epicsTimeStamp startTime;
            epicsTimeGetCurrent(&startTime);
            pArray->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;
            updateTimeStamp(&pArray->epicsTS);

            // Get any attributes that have been defined for this driver
            this->getAttributes(pArray->pAttributeList);

            // Call the NDArray callback
            if (arrayCallbacks)
                doCallbacksGenericPointer(pArray, NDArrayData, 0);

            lock();
            setIntegerParam(NDArrayCounter, ++imageCounter);
            unlock();

            callParamCallbacks();
            pArray->release();
        }

end:
        int dropped = 0;
        mApi.getInt(SSStreamStatus, "dropped", &dropped);

        lock();
        setIntegerParam(EigerStreamDropped, dropped);
        unlock();

        mStreamDoneEvent.signal();
    }
}

asynStatus eigerDetector::initParams (void)
{
    int status = asynSuccess;

    // Assume 'description' is of the form 'Dectris Eiger 1M'
    char desc[MAX_BUF_SIZE] = "";
    char *manufacturer, *space, *model;
    status = mApi.getString(SSDetConfig, "description", desc, sizeof(desc));
    space = strchr(desc, ' ');
    *space = '\0';
    manufacturer = desc;
    model = space + 1;

    status |= setStringParam (ADManufacturer, manufacturer);
    status |= setStringParam (ADModel, model);

    // Get frame dimensions
    int maxSizeX, maxSizeY;
    status |= mApi.getInt(SSDetConfig, "x_pixels_in_detector", &maxSizeX);
    status |= mApi.getInt(SSDetConfig, "y_pixels_in_detector", &maxSizeY);

    status |= setIntegerParam(ADMaxSizeX, maxSizeX);
    status |= setIntegerParam(ADMaxSizeY, maxSizeY);
    status |= setIntegerParam(ADSizeX, maxSizeX);
    status |= setIntegerParam(ADSizeY, maxSizeY);
    status |= setIntegerParam(NDArraySizeX, maxSizeX);
    status |= setIntegerParam(NDArraySizeY, maxSizeY);

    // Read all the following parameters into their respective asyn params
    status |= getStringP(SSDetConfig, "software_version", EigerSWVersion);
    status |= getStringP(SSDetConfig, "detector_number",  EigerSerialNumber);

    status |= getDoubleP(SSDetConfig, "count_time",       ADAcquireTime);
    status |= getDoubleP(SSDetConfig, "frame_time",       ADAcquirePeriod);
    status |= getIntP   (SSDetConfig, "nimages",          ADNumImages);
    status |= getDoubleP(SSDetConfig, "photon_energy",    EigerPhotonEnergy);
    status |= getDoubleP(SSDetConfig, "threshold_energy", EigerThreshold);
    status |= getIntP   (SSDetConfig, "ntrigger",         EigerNTriggers);

    status |= getBoolP  (SSFWConfig, "compression_enabled",EigerFWCompression);
    status |= getStringP(SSFWConfig, "name_pattern",       EigerFWNamePattern);
    status |= getIntP   (SSFWConfig, "nimages_per_file",   EigerFWNImgsPerFile);
    status |= getIntP   (SSFWStatus, "buffer_free",        EigerFWFree);

    status |= getDoubleP(SSDetConfig, "beam_center_x",     EigerBeamX);
    status |= getDoubleP(SSDetConfig, "beam_center_y",     EigerBeamY);
    status |= getDoubleP(SSDetConfig, "detector_distance", EigerDetDist);
    status |= getBoolP  (SSDetConfig, "flatfield_correction_applied",
            EigerFlatfield);
    status |= getDoubleP(SSDetConfig, "wavelength",        EigerWavelength);

    // Read enabled modules
    char mode[MAX_BUF_SIZE];
    mApi.getString(SSMonConfig, "mode", mode, sizeof(mode));
    status |= setIntegerParam(EigerMonitorEnable, mode[0] == 'e');

    mApi.getString(SSFWConfig, "mode", mode, sizeof(mode));
    status |= setIntegerParam(EigerFWEnable, mode[0] == 'e');

    mApi.getString(SSStreamConfig, "mode", mode, sizeof(mode));
    status |= setIntegerParam(EigerStreamEnable, mode[0] == 'e');

    // Set some default values
    status |= setIntegerParam(NDArraySize, 0);
    status |= setIntegerParam(NDDataType,  NDUInt32);
    status |= setIntegerParam(ADImageMode, ADImageMultiple);
    status |= setIntegerParam(EigerArmed,  0);
    status |= setIntegerParam(EigerSaveFiles, 1);
    status |= setIntegerParam(EigerSequenceId, 0);
    status |= setIntegerParam(EigerPendingFiles, 0);
    status |= setIntegerParam(EigerMonitorEnable, 0);
    status |= setDoubleParam (EigerMonitorPeriod, MONITOR_MIN_PERIOD);

    callParamCallbacks();

    // Set more parameters

    // Auto Summation should always be true (SIMPLON API Reference v1.3.0)
    status |= putBool(SSDetConfig, "auto_summation", true);

    // This driver expects the following parameters to always have the same value
    status |= putString(SSStreamConfig, "header_detail", "none");
    status |= putInt(SSFWConfig, "image_nr_start", DEFAULT_NR_START);
    status |= putInt(SSMonConfig, "buffer_size", 1);

    return (asynStatus)status;
}

/* Functions named get<type>P get the detector parameter of type <type> and
 * name 'param' and set the asyn parameter of index 'dest' with its value.
 */

asynStatus eigerDetector::getStringP (sys_t sys, const char *param, int dest)
{
    int status;
    char value[MAX_BUF_SIZE];

    status = mApi.getString(sys, param, value, sizeof(value)) |
            setStringParam(dest, value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getIntP (sys_t sys, const char *param, int dest)
{
    int status;
    int value;

    status = mApi.getInt(sys, param, &value) | setIntegerParam(dest,value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getDoubleP (sys_t sys, const char *param, int dest)
{
    int status;
    double value;

    status = mApi.getDouble(sys, param, &value) | setDoubleParam(dest, value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getBoolP (sys_t sys, const char *param, int dest)
{
    int status;
    bool value;

    status = mApi.getBool(sys, param, &value) | setIntegerParam(dest, (int)value);
    return (asynStatus)status;
}

/* The following functions are wrappers on the functions with the same name from
 * the Eiger API. The wrapper takes care of updating the parameters list if
 * more parameters are changed as an effect of a put.
 */
asynStatus eigerDetector::putString (sys_t sys, const char *param,
        const char *value)
{
    const char *functionName = "putString";
    paramList_t paramList;

    if(mApi.putString(sys, param, value, &paramList))
    {
        ERR_ARGS("[param=%s] underlying put failed", param);
        return asynError;
    }

    updateParams(&paramList);

    return asynSuccess;
}

asynStatus eigerDetector::putInt (sys_t sys, const char *param, int value)
{
    const char *functionName = "putInt";
    paramList_t paramList;

    if(mApi.putInt(sys, param, value, &paramList))
    {
        ERR_ARGS("[param=%s] underlying put failed", param);
        return asynError;
    }

    updateParams(&paramList);

    return asynSuccess;
}

asynStatus eigerDetector::putBool (sys_t sys, const char *param, bool value)
{
    const char *functionName = "putBool";
    paramList_t paramList;

    if(mApi.putBool(sys, param, value, &paramList))
    {
        ERR("underlying eigerPutBool failed");
        return asynError;
    }

    updateParams(&paramList);

    return asynSuccess;
}

asynStatus eigerDetector::putDouble (sys_t sys, const char *param,
        double value)
{
    const char *functionName = "putDouble";
    paramList_t paramList;

    if(mApi.putDouble(sys, param, value, &paramList))
    {
        ERR_ARGS("[param=%s] underlying put failed", param);
        return asynError;
    }

    updateParams(&paramList);

    return asynSuccess;
}

void eigerDetector::updateParams(paramList_t *paramList)
{
    for(int i = 0; i < paramList->nparams; ++i)
    {
        if(!strcmp(paramList->params[i], "count_time"))
            getDoubleP (SSDetConfig, "count_time", ADAcquireTime);
        else if(!strcmp(paramList->params[i], "frame_time"))
            getDoubleP (SSDetConfig, "frame_time", ADAcquirePeriod);
        else if(!strcmp(paramList->params[i], "nimages"))
            getIntP (SSDetConfig, "nimages", ADNumImages);
        else if(!strcmp(paramList->params[i], "photon_energy"))
            getDoubleP(SSDetConfig, "photon_energy", EigerPhotonEnergy);
        else if(!strcmp(paramList->params[i], "beam_center_x"))
            getDoubleP(SSDetConfig, "beam_center_x", EigerBeamX);
        else if(!strcmp(paramList->params[i], "beam_center_y"))
            getDoubleP(SSDetConfig, "beam_center_y", EigerBeamY);
        else if(!strcmp(paramList->params[i], "detector_distance"))
            getDoubleP(SSDetConfig, "detector_distance", EigerDetDist);
        else if(!strcmp(paramList->params[i], "threshold_energy"))
            getDoubleP(SSDetConfig, "threshold_energy", EigerThreshold);
        else if(!strcmp(paramList->params[i], "wavelength"))
            getDoubleP(SSDetConfig, "wavelength", EigerWavelength);
    }
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
    fId = H5LTopen_file_image((void*)buf, bufLen, flags);
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
        pImage->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;
        updateTimeStamp(&pImage->epicsTS);

        // Get any attributes that have been defined for this driver
        this->getAttributes(pImage->pAttributeList);

        // Call the NDArray callback
        getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
        if (arrayCallbacks)
        {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: calling NDArray callback\n",
                    driverName, functionName);

            doCallbacksGenericPointer(pImage, NDArrayData, 0);
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

/*
 * Makes lots of assumptions on the file layout
 */
asynStatus eigerDetector::parseTiffFile (char *buf, size_t len)
{
    const char *functionName = "parseTiffFile";

    if(*(uint32_t*)buf != 0x0002A4949)
    {
        ERR("wrong tiff header");
        return asynError;
    }

    uint32_t offset     = *((uint32_t*)(buf+4));
    uint16_t numEntries = *((uint16_t*)(buf+offset));

    typedef struct
    {
        uint16_t id;
        uint16_t type;
        uint32_t count;
        uint32_t offset;
    }tag_t;

    tag_t *tags = (tag_t*)(buf + offset + 2);

    size_t width = 0, height = 0, depth = 0, dataLen = 0;

    for(size_t i = 0; i < numEntries; ++i)
    {
        switch(tags[i].id)
        {
        case 256: width   = tags[i].offset; break;
        case 257: height  = tags[i].offset; break;
        case 258: depth   = tags[i].offset; break;
        case 279: dataLen = tags[i].offset; break;
        }
    }

    if(!width || !height || !depth || !dataLen)
    {
        ERR("missing tags");
        return asynError;
    }

    NDDataType_t dataType;
    switch(depth)
    {

    case 8:  dataType = NDUInt8;    break;
    case 16: dataType = NDUInt16;   break;
    case 32: dataType = NDUInt32;   break;
    default:
        ERR_ARGS("unexpected bit depth=%lu", depth);
        return asynError;
    }

    size_t dims[2] = {width, height};

    NDArray *pImage = pNDArrayPool->alloc(2, dims, dataType, 0, NULL);
    if(!pImage)
    {
        ERR("couldn't allocate NDArray");
        return asynError;
    }

    memcpy(pImage->pData, buf+8, dataLen);
    doCallbacksGenericPointer(pImage, NDArrayData, 1);
    pImage->release();

    return asynSuccess;
}

/* This function is called periodically read the detector status (temperature,
 * humidity, etc.).
 */
asynStatus eigerDetector::eigerStatus (void)
{
    const char *functionName = "eigerStatus";
    int status;
    double temp = 0.0;
    double humid = 0.0;
    char link[4][MAX_BUF_SIZE];
    char state[MAX_BUF_SIZE];
    char error[MAX_BUF_SIZE];
    double dcuBuffer;

    // Read state and error message
    status  = mApi.getString(SSDetStatus, "state", state, sizeof(state));
    status |= mApi.getString(SSDetStatus, "error", error, sizeof(error));

    // Read temperature and humidity
    status |= mApi.getDouble(SSDetStatus, "board_000/th0_temp",     &temp);
    status |= mApi.getDouble(SSDetStatus, "board_000/th0_humidity", &humid);

    // Read the status of each individual link between the head and the server
    status |= mApi.getString(SSDetStatus, "link_0", link[0], sizeof(link[0]));
    status |= mApi.getString(SSDetStatus, "link_1", link[1], sizeof(link[1]));
    status |= mApi.getString(SSDetStatus, "link_2", link[2], sizeof(link[2]));
    status |= mApi.getString(SSDetStatus, "link_3", link[3], sizeof(link[3]));

    status |= mApi.getDouble(SSDetStatus, "builder/dcu_buffer_free", &dcuBuffer);

    if(!status)
    {
        setStringParam(EigerState, state);
        setStringParam(EigerError, error);
        setDoubleParam(ADTemperatureActual, temp);
        setDoubleParam(EigerThTemp0,  temp);
        setDoubleParam(EigerThHumid0, humid);
        setIntegerParam(EigerLink0, !strcmp(link[0], "up"));
        setIntegerParam(EigerLink1, !strcmp(link[1], "up"));
        setIntegerParam(EigerLink2, !strcmp(link[2], "up"));
        setIntegerParam(EigerLink3, !strcmp(link[3], "up"));
        setDoubleParam(EigerDCUBufFree, dcuBuffer);

        callParamCallbacks();
    }
    else
        ERR("error updating status");

    return (asynStatus)status;
}

extern "C" int eigerDetectorConfig(const char *portName, const char *serverPort,
        int maxBuffers, size_t maxMemory, int priority, int stackSize)
{
    new eigerDetector(portName, serverPort, maxBuffers, maxMemory, priority,
            stackSize);
    return(asynSuccess);
}

// Code for iocsh registration
static const iocshArg eigerDetectorConfigArg0 = {"Port name", iocshArgString};
static const iocshArg eigerDetectorConfigArg1 = {"Server host name",
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

