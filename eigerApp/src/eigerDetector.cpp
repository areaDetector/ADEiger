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
#include "eigerApi.h"

#define MAX_BUF_SIZE            256
#define DEFAULT_NR_START        1
#define DEFAULT_QUEUE_CAPACITY  2

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
    bool saveFiles;
}acquisition_t;

typedef struct
{
    char name[MAX_BUF_SIZE];
    char *data;
    size_t len;
    bool save, stream, failed;
    size_t refCount;
}file_t;

static const char *driverName = "eigerDetector";

static inline size_t numDataFiles (int nTriggers, int nImages, int nImagesPerFile)
{
    return (size_t) nTriggers*ceil(((double)nImages)/((double)nImagesPerFile));
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

static void streamTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->streamTask();
}

static void saveTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->saveTask();
}

static void reapTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->reapTask();
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
               ASYN_CANBLOCK,    /* ASYN_CANBLOCK=1, ASYN_MULTIDEVICE=0 */
               1,                /* autoConnect=1 */
               priority, stackSize),
    mEiger(serverHostname),
    mStartEvent(), mStopEvent(), mTriggerEvent(), mPollDoneEvent(),
    mPollQueue(1, sizeof(acquisition_t)),
    mDownloadQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mStreamQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mSaveQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mReapQueue(DEFAULT_QUEUE_CAPACITY*2, sizeof(file_t *))
{

    int status = asynSuccess;
    const char *functionName = "eigerDetector";

    strncpy(mHostname, serverHostname, sizeof(mHostname));

    // Initialize sockets
    Eiger::init();

    // FileWriter Parameters
    createParam(EigerFWClearString,       asynParamInt32, &EigerFWClear);
    createParam(EigerFWCompressionString, asynParamInt32, &EigerFWCompression);
    createParam(EigerFWNamePatternString, asynParamOctet, &EigerFWNamePattern);
    createParam(EigerFWNImgsPerFileString,asynParamInt32, &EigerFWNImgsPerFile);

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
    createParam(EigerThTemp0String,       asynParamFloat64, &EigerThTemp0);
    createParam(EigerThHumid0String,      asynParamFloat64, &EigerThHumid0);
    createParam(EigerLink0String,         asynParamInt32,   &EigerLink0);
    createParam(EigerLink1String,         asynParamInt32,   &EigerLink1);
    createParam(EigerLink2String,         asynParamInt32,   &EigerLink2);
    createParam(EigerLink3String,         asynParamInt32,   &EigerLink3);

    // Other Parameters
    createParam(EigerArmedString,         asynParamInt32,   &EigerArmed);
    createParam(EigerSaveFilesString,     asynParamInt32,   &EigerSaveFiles);
    createParam(EigerSequenceIdString,    asynParamInt32,   &EigerSequenceId);
    createParam(EigerPendingFilesString,  asynParamInt32,   &EigerPendingFiles);

    status = asynSuccess;

    // Set some default values for parameters

    // Extract Manufacturer and Model from 'description'
    char desc[MAX_BUF_SIZE] = "";
    char *manufacturer, *space, *model;

    if(mEiger.getString(SSDetConfig, "description", desc, sizeof(desc)))
    {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Eiger seems to be uninitialized\n"
                "Initializing... (may take a while)\n",
                driverName, functionName);

        if(mEiger.initialize())
        {
            ERR("Eiger FAILED TO INITIALIZE");
            return;
        }

        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s Eiger initialized\n",
                    driverName, functionName);

        status = mEiger.getString(SSDetConfig, "description", desc, sizeof(desc));
    }

    // Assume 'description' is of the form 'Dectris Eiger 1M'
    space = strchr(desc, ' ');
    *space = '\0';
    manufacturer = desc;
    model = space + 1;

    status |= setStringParam (ADManufacturer, manufacturer);
    status |= setStringParam (ADModel, model);

    // Get software (detector firmware) version and serial number
    char swVersion[MAX_BUF_SIZE];
    status |= mEiger.getString(SSDetConfig, "software_version", swVersion, sizeof(swVersion));
    status |= setStringParam(EigerSWVersion, swVersion);

    char serialNumber[MAX_BUF_SIZE];
    status |= mEiger.getString(SSDetConfig, "detector_number", serialNumber, sizeof(serialNumber));
    status |= setStringParam(EigerSerialNumber, serialNumber);

    // Get frame dimensions
    int maxSizeX, maxSizeY;
    status |= mEiger.getInt(SSDetConfig, "x_pixels_in_detector", &maxSizeX);
    status |= mEiger.getInt(SSDetConfig, "y_pixels_in_detector", &maxSizeY);

    status |= setIntegerParam(ADMaxSizeX, maxSizeX);
    status |= setIntegerParam(ADMaxSizeY, maxSizeY);
    status |= setIntegerParam(ADSizeX, maxSizeX);
    status |= setIntegerParam(ADSizeY, maxSizeY);
    status |= setIntegerParam(NDArraySizeX, maxSizeX);
    status |= setIntegerParam(NDArraySizeY, maxSizeY);

    // Read all the following parameters into their respective asyn params
    status |= getDoubleP(SSDetConfig, "count_time",       ADAcquireTime);
    status |= getDoubleP(SSDetConfig, "frame_time",       ADAcquirePeriod);
    status |= getIntP   (SSDetConfig, "nimages",          ADNumImages);
    status |= getDoubleP(SSDetConfig, "photon_energy",    EigerPhotonEnergy);
    status |= getDoubleP(SSDetConfig, "threshold_energy", EigerThreshold);
    status |= getIntP   (SSDetConfig, "ntrigger",         EigerNTriggers);

    status |= getBoolP  (SSFWConfig, "compression_enabled",EigerFWCompression);
    status |= getStringP(SSFWConfig, "name_pattern",       EigerFWNamePattern);
    status |= getIntP   (SSFWConfig, "nimages_per_file",   EigerFWNImgsPerFile);

    status |= getDoubleP(SSDetConfig, "beam_center_x",     EigerBeamX);
    status |= getDoubleP(SSDetConfig, "beam_center_y",     EigerBeamY);
    status |= getDoubleP(SSDetConfig, "detector_distance", EigerDetDist);
    status |= getBoolP  (SSDetConfig, "flatfield_correction_applied",
            EigerFlatfield);
    status |= getDoubleP(SSDetConfig, "threshold_energy",  EigerThreshold);
    status |= getDoubleP(SSDetConfig, "wavelength",        EigerWavelength);

    // Set some default values
    status |= setIntegerParam(NDArraySize, 0);
    status |= setIntegerParam(NDDataType,  NDUInt32);
    status |= setIntegerParam(ADImageMode, ADImageMultiple);
    status |= setIntegerParam(EigerArmed,  0);
    status |= setIntegerParam(EigerSaveFiles, 1);
    status |= setIntegerParam(EigerSequenceId, 0);
    status |= setIntegerParam(EigerPendingFiles, 0);

    // Read status once at startup
    eigerStatus();

    callParamCallbacks();

    // Set more parameters

    // Auto Summation should always be true (SIMPLON API Reference v1.3.0)
    status |= putBool(SSDetConfig, "auto_summation", true);

    // FileWriter should always be enabled
    status |= putString(SSFWConfig, "mode", "enabled");

    // This driver expects the following parameters to always have the same value
    status |= putInt(SSFWConfig, "image_nr_start", DEFAULT_NR_START);

    if(status)
    {
        ERR("unable to set detector parameters");
        return;
    }

    // Control task
    status = (epicsThreadCreate("eigerControlTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)controlTaskC, this) == NULL);

    // Poll task
    status = (epicsThreadCreate("eigerPollTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)pollTaskC, this) == NULL);

    // Download task
    status = (epicsThreadCreate("eigerDownloadTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)downloadTaskC, this) == NULL);

    // Stream task
    status = (epicsThreadCreate("eigerStreamTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)streamTaskC, this) == NULL);

    // Save task
    status = (epicsThreadCreate("eigerSaveTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)saveTaskC, this) == NULL);

    // Reap task
    status = (epicsThreadCreate("eigerReapTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)reapTaskC, this) == NULL);

    if(status)
    {
        ERR("epicsThreadCreate failure for some task");
        return;
    }
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
            mEiger.abort();
            lock();
            setIntegerParam(ADStatus, ADStatusAborted);
            mStopEvent.signal();
        }
    }
    else if (function == EigerFWClear)
        status = putInt(SSFWConfig, "clear", 1);
    else if (function == EigerFWCompression)
        status = putBool(SSFWConfig, "compression_enabled", (bool)value);
    else if (function == EigerFWNImgsPerFile)
        status = putInt(SSFWConfig, "nimages_per_file", value);
    else if (function == EigerFlatfield)
        status = putBool(SSDetConfig, "flatfield_correction_applied", (bool)value);
    else if (function == EigerNTriggers)
        status = putInt(SSDetConfig, "ntrigger", value);
    else if (function == ADTriggerMode)
        status = putString(SSDetConfig, "trigger_mode", Eiger::triggerModeStr[value]);
    else if (function == ADNumImages)
        status = putInt(SSDetConfig, "nimages", value);
    else if (function == ADReadStatus)
        status = eigerStatus();
    else if (function == EigerTrigger)
        mTriggerEvent.signal();
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
  * This function performs actions for some parameters, including
  * eigerBadPixelFile, ADFilePath, etc.
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
    Eiger ctrlEiger(mHostname);
    const char *functionName = "controlTask";
    acquisition_t acquisition;

    int status = asynSuccess;
    int adStatus, manualTrigger;
    int sequenceId, saveFiles, numImages, numTriggers, triggerMode, numImagesPerFile;
    double acquirePeriod, triggerTimeout = 0.0, triggerExposure;

    lock();

    for(;;)
    {
        // Clear uncaught events
        mStopEvent.tryWait();
        mTriggerEvent.tryWait();
        mPollDoneEvent.tryWait();

        // Wait for start event
        getIntegerParam(ADStatus, &adStatus);
        if(adStatus == ADStatusIdle)
            setStringParam(ADStatusMessage, "Waiting for acquire command");
        callParamCallbacks();

        unlock();
        mStartEvent.wait();
        lock();

        // If saving files, check if the File Path is valid
        getIntegerParam(EigerSaveFiles, &saveFiles);

        if(saveFiles)
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

        // Latch parameters
        getIntegerParam(EigerFWNImgsPerFile, &numImagesPerFile);
        getDoubleParam (ADAcquirePeriod,     &acquirePeriod);
        getIntegerParam(ADNumImages,         &numImages);
        getIntegerParam(EigerNTriggers,      &numTriggers);
        getIntegerParam(ADTriggerMode,       &triggerMode);
        getIntegerParam(EigerManualTrigger,  &manualTrigger);

        // Arm the detector
        setStringParam(ADStatusMessage, "Arming...");
        callParamCallbacks();

        unlock();
        status = ctrlEiger.arm(&sequenceId);
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

        // Start polling
        getStringParam(EigerFWNamePattern, sizeof(acquisition.pattern), acquisition.pattern);
        acquisition.sequenceId = sequenceId;
        acquisition.nDataFiles = numDataFiles(numTriggers, numImages, numImagesPerFile);
        acquisition.saveFiles  = saveFiles;
        mPollQueue.send(&acquisition, sizeof(acquisition));

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
                    status = ctrlEiger.trigger(triggerTimeout, triggerExposure);
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
        status = ctrlEiger.disarm();
        lock();
        setIntegerParam(EigerArmed, 0);
        setStringParam(ADStatusMessage, "Waiting for files to be processed...");
        callParamCallbacks();

        unlock();
        mPollDoneEvent.wait();
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
    Eiger pollEiger(mHostname);
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

            files[i].save     = (bool)acquisition.saveFiles;
            files[i].stream   = !isMaster;
            files[i].refCount = files[i].save + files[i].stream;

            if(isMaster)
                Eiger::buildMasterName(acquisition.pattern, acquisition.sequenceId,
                        files[i].name, sizeof(files[i].name));
            else
                Eiger::buildDataName(i-1+DEFAULT_NR_START, acquisition.pattern,
                        acquisition.sequenceId, files[i].name,
                        sizeof(files[i].name));
        }

        // While acquiring, wait and download every file on the list
        i = 0;
        do
        {
            file_t *curFile = &files[i];

            if(!pollEiger.waitFile(curFile->name, 1.0))
            {
                if(curFile->save || curFile->stream)
                {
                    mDownloadQueue.send(&curFile, sizeof(curFile));

                    lock();
                    getIntegerParam(EigerPendingFiles, &pendingFiles);
                    setIntegerParam(EigerPendingFiles, pendingFiles+1);
                    unlock();
                }
                ++i;
            }

            lock();
            getIntegerParam(ADStatus, &adStatus);
            unlock();
        }while((adStatus == ADStatusAcquire || ADStatus == ADStatusReadout)
                && i < totalFiles);

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
    Eiger dlEiger(mHostname);
    const char *functionName = "downloadTask";
    file_t *file;

    for(;;)
    {
        mDownloadQueue.receive(&file, sizeof(file_t *));

        FLOW_ARGS("file=%s", file->name);

        file->refCount = file->stream + file->save;

        // Download the file
        if(dlEiger.getFile(file->name, &file->data, &file->len))
        {
            ERR_ARGS("underlying getFile(%s) failed", file->name);
            file->failed = true;
            mReapQueue.send(&file, sizeof(file));
        }
        else
        {
            if(file->stream)
                mStreamQueue.send(&file, sizeof(file_t *));

            if(file->save)
                mSaveQueue.send(&file, sizeof(file_t *));
        }
    }
}

void eigerDetector::streamTask (void)
{
    const char *functionName = "streamTask";
    file_t *file;

    for(;;)
    {
        mStreamQueue.receive(&file, sizeof(file_t *));

        FLOW_ARGS("file=%s", file->name);

        if(parseH5File(file->data, file->len))
        {
            ERR_ARGS("underlying parseH5File(%s) failed", file->name);
            file->failed = true;
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
            file->failed = true;
            goto reap;
        }

        written = fwrite(file->data, 1, file->len, fhandle);
        if(written < file->len)
        {
            ERR_ARGS("[file=%s] failed to write to local file (%lu written)",
                    file->name, written);
            file->failed = true;
        }
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

/* Functions named get<type>P get the detector parameter of type <type> and
 * name 'param' and set the asyn parameter of index 'dest' with its value.
 */

asynStatus eigerDetector::getStringP (sys_t sys, const char *param, int dest)
{
    int status;
    char value[MAX_BUF_SIZE];

    status = mEiger.getString(sys, param, value, sizeof(value)) |
            setStringParam(dest, value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getIntP (sys_t sys, const char *param, int dest)
{
    int status;
    int value;

    status = mEiger.getInt(sys, param, &value) | setIntegerParam(dest,value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getDoubleP (sys_t sys, const char *param, int dest)
{
    int status;
    double value;

    status = mEiger.getDouble(sys, param, &value) | setDoubleParam(dest, value);
    return (asynStatus)status;
}

asynStatus eigerDetector::getBoolP (sys_t sys, const char *param, int dest)
{
    int status;
    bool value;

    status = mEiger.getBool(sys, param, &value) | setIntegerParam(dest, (int)value);
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

    if(mEiger.putString(sys, param, value, &paramList))
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

    if(mEiger.putInt(sys, param, value, &paramList))
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

    if(mEiger.putBool(sys, param, value, &paramList))
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

    if(mEiger.putDouble(sys, param, value, &paramList))
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

/* This function is called periodically read the detector status (temperature,
 * humidity, etc.).
 */
asynStatus eigerDetector::eigerStatus (void)
{
    int status;
    double temp = 0.0;
    double humid = 0.0;
    char link[4][MAX_BUF_SIZE];

    // Read temperature and humidity
    status  = mEiger.getDouble(SSDetStatus, "board_000/th0_temp",     &temp);
    status |= mEiger.getDouble(SSDetStatus, "board_000/th0_humidity", &humid);
    status |= mEiger.getString(SSDetStatus, "link_0", link[0], sizeof(link[0]));
    status |= mEiger.getString(SSDetStatus, "link_1", link[1], sizeof(link[1]));
    status |= mEiger.getString(SSDetStatus, "link_2", link[2], sizeof(link[2]));
    status |= mEiger.getString(SSDetStatus, "link_3", link[3], sizeof(link[3]));

    if(!status)
    {
        setDoubleParam(ADTemperatureActual, temp);
        setDoubleParam(EigerThTemp0,  temp);
        setDoubleParam(EigerThHumid0, humid);
        setIntegerParam(EigerLink0, !strcmp(link[0], "up"));
        setIntegerParam(EigerLink1, !strcmp(link[1], "up"));
        setIntegerParam(EigerLink2, !strcmp(link[2], "up"));
        setIntegerParam(EigerLink3, !strcmp(link[3], "up"));
    }
    else
        setIntegerParam(ADStatus, ADStatusError);

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

