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
#include <string.h>
#include <math.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>

#include <limits>

#include <hdf5.h>
#include <hdf5_hl.h>

#include "ADDriver.h"
#include "eigerDetector.h"
#include "restApi.h"
#include "streamApi.h"

// Set this flag if you are using the pre-release firmware that supports External Gate mode
#define HAVE_EXTG_FIRMWARE      1
#define MAX_BUF_SIZE            256
#define DEFAULT_NR_START        1
#define DEFAULT_QUEUE_CAPACITY  2

#define MX_PARAM_EPSILON        0.0001
#define ENERGY_EPSILON          0.05
#define WAVELENGTH_EPSILON      0.0005

// Error message formatters
#define ERR(msg) asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s::%s: %s\n", \
    driverName, functionName, msg)

#define ERR_ARGS(fmt,...) asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, \
    "%s::%s: " fmt "\n", driverName, functionName, __VA_ARGS__);

// Flow message formatters
#define FLOW(msg) asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s::%s: %s\n", \
    driverName, functionName, msg)

#define FLOW_ARGS(fmt,...) asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, \
    "%s::%s: " fmt "\n", driverName, functionName, __VA_ARGS__);

using std::string;
using std::vector;
using std::map;

static const string DRIVER_VERSION("3.6.0");

enum data_source
{
    SOURCE_NONE,
    SOURCE_FILEWRITER,
    SOURCE_STREAM,
};

typedef struct
{
    char pattern[MAX_BUF_SIZE];
    int sequenceId;
    size_t nDataFiles;
    bool saveFiles, parseFiles, removeFiles;
    mode_t filePerms;
}acquisition_t;

typedef struct
{
    char name[MAX_BUF_SIZE];
    char *data;
    size_t len;
    bool save, parse, remove;
    size_t refCount;
    uid_t uid, gid;
    mode_t perms;
}file_t;

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

static void initializeTaskC (void *drvPvt)
{
    ((eigerDetector *)drvPvt)->initializeTask();
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

    : ADDriver(portName, 2, 0, maxBuffers, maxMemory,
               0, 0,             /* No interfaces beyond ADDriver.cpp */
               ASYN_CANBLOCK |   /* ASYN_CANBLOCK=1 */
               ASYN_MULTIDEVICE, /* ASYN_MULTIDEVICE=1 */
               1,                /* autoConnect=1 */
               priority, stackSize),
    mApi(serverHostname, 80),
    mStreamAPI(0), mStream2API(0),
    mStartEvent(), mStopEvent(), mTriggerEvent(), mPollDoneEvent(),
    mPollQueue(1, sizeof(acquisition_t)),
    mDownloadQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mParseQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mSaveQueue(DEFAULT_QUEUE_CAPACITY, sizeof(file_t *)),
    mReapQueue(DEFAULT_QUEUE_CAPACITY*2, sizeof(file_t *)),
    mFrameNumber(0), mFsUid(getuid()), mFsGid(getgid()),
    mParams(this, &mApi, pasynUserSelf)
{
    const char *functionName = "eigerDetector";
    strncpy(mHostname, serverHostname, sizeof(mHostname)-1);

    // Get API version
    mAPIVersion = mApi.getAPIVersion();

    // Write version to appropriate parameter
    setStringParam(NDDriverVersion, DRIVER_VERSION);

    // Generate subSystemMap
    mSubSystemMap.insert(std::make_pair("DS", SSDetStatus));
    mSubSystemMap.insert(std::make_pair("DC", SSDetConfig));
    mSubSystemMap.insert(std::make_pair("FS", SSFWStatus));
    mSubSystemMap.insert(std::make_pair("FC", SSFWConfig));
    mSubSystemMap.insert(std::make_pair("MS", SSMonStatus));
    mSubSystemMap.insert(std::make_pair("MC", SSMonConfig));
    mSubSystemMap.insert(std::make_pair("SS", SSStreamStatus));
    mSubSystemMap.insert(std::make_pair("SC", SSStreamConfig));

    // Detector Status Parameters
    mState      = mParams.create(EigStateStr,      asynParamOctet,   SSDetStatus, "state");
    mFirstParam = mState->getIndex();

    // Test if the detector is initialized
    if(mState->fetch())
    {
        ERR("Cannot fetch state. Eiger could be disconnected.");
        setStringParam(ADStatusMessage, "Eiger FAILED TO CONNECT");
        return;
    }
    std::string state;
    mState->get(state);
    if(state == "na")
    {
        ERR("Eiger seems to be uninitialized\nInitializing... (may take a while)");

        if(mApi.initialize())
        {
            ERR("Eiger FAILED TO INITIALIZE");
            setStringParam(ADStatusMessage, "Eiger FAILED TO INITIALIZE");
            return;
        }

        int sequenceId = 0;
        if(mApi.arm(&sequenceId))
        {
            ERR("Failed to arm the detector for the first time");
            setStringParam(ADStatusMessage, "Eiger failed to arm for the first time");
        }
        else
        {
            mSequenceId->put(sequenceId);
            mApi.disarm();
        }

        FLOW("Eiger initialized");
    }

    // Driver-only parameters
    mDataSource     = mParams.create(EigDataSourceStr,     asynParamInt32);

    mFWAutoRemove   = mParams.create(EigFWAutoRemoveStr,   asynParamInt32);
    mTrigger        = mParams.create(EigTriggerStr,        asynParamInt32);
    mTriggerExp     = mParams.create(EigTriggerExpStr,     asynParamFloat64);
    mManualTrigger  = mParams.create(EigManualTriggerStr,  asynParamInt32);
    mArmed          = mParams.create(EigArmedStr,          asynParamInt32);
    mSequenceId     = mParams.create(EigSequenceIdStr,     asynParamInt32);
    mPendingFiles   = mParams.create(EigPendingFilesStr,   asynParamInt32);
    mSaveFiles      = mParams.create(EigSaveFilesStr,      asynParamInt32);
    mFileOwner      = mParams.create(EigFileOwnerStr,      asynParamOctet);
    mFileOwnerGroup = mParams.create(EigFileOwnerGroupStr, asynParamOctet);
    mFilePerms      = mParams.create(EigFilePermsStr,      asynParamInt32);
    mMonitorTimeout = mParams.create(EigMonitorTimeoutStr, asynParamInt32);
    mInitialize     = mParams.create(EigInitializeStr,     asynParamInt32);
    mStreamDecompress = mParams.create(EigStreamDecompressStr, asynParamInt32);
    mWavelengthEpsilon = mParams.create(EigWavelengthEpsilonStr, asynParamFloat64);
    mEnergyEpsilon  = mParams.create(EigEnergyEpsilonStr,  asynParamFloat64);
    mSignedData     = mParams.create(EigSignedDataStr,     asynParamInt32);

    // Metadata
    mDescription = mParams.create(EigDescriptionStr, asynParamOctet, SSDetConfig, "description");
    // Get the Eiger model from the description
    string description;
    mDescription->fetch(description);
    mEigerModel = Eiger1;
    if ((description.find("Eiger2") != std::string::npos) || (description.find("EIGER2") != std::string::npos))	
        mEigerModel = Eiger2;
    else if ((description.find("Pilatus4") != std::string::npos) || (description.find("PILATUS4") != std::string::npos))
        mEigerModel = Pilatus4;


    // Work around weird ordering
    vector<string> modeEnum;
    modeEnum.reserve(2);
    modeEnum.push_back("disabled");
    modeEnum.push_back("enabled");    

    // Acquisition
    mWavelength       = mParams.create(EigWavelengthStr,      asynParamFloat64, SSDetConfig, "wavelength");
    mWavelength->setEpsilon(WAVELENGTH_EPSILON);
    mPhotonEnergy     = mParams.create(EigPhotonEnergyStr,    asynParamFloat64, SSDetConfig, "photon_energy");
    mPhotonEnergy->setEpsilon(ENERGY_EPSILON);
    mThreshold        = mParams.create(EigThresholdStr,       asynParamFloat64, SSDetConfig, "threshold_energy");
    mThreshold->setEpsilon(ENERGY_EPSILON);
    mNTriggers        = mParams.create(EigNTriggersStr,       asynParamInt32,   SSDetConfig, "ntrigger");
    mCompressionAlgo  = mParams.create(EigCompressionAlgoStr, asynParamInt32,   SSDetConfig, "compression");
    mROIMode          = mParams.create(EigROIModeStr,         asynParamInt32,   SSDetConfig, "roi_mode");
    mAutoSummation    = mParams.create(EigAutoSummationStr,   asynParamInt32,   SSDetConfig, "auto_summation");

    // Detector Status Parameters
    mError      = mParams.create(EigErrorStr,      asynParamOctet,   SSDetStatus, "error");
    mThTemp0    = mParams.create(EigThTemp0Str,    asynParamFloat64, SSDetStatus, "board_000/th0_temp");
    mThHumid0   = mParams.create(EigThHumid0Str,   asynParamFloat64, SSDetStatus, "board_000/th0_humidity");

    // File Writer
    mFWEnable       = mParams.create(EigFWEnableStr,       asynParamInt32, SSFWConfig,  "mode");
    mFWEnable->setEnumValues(modeEnum);
    mFWCompression  = mParams.create(EigFWCompressionStr,  asynParamInt32, SSFWConfig,  "compression_enabled");
    mFWNamePattern  = mParams.create(EigFWNamePatternStr,  asynParamOctet, SSFWConfig,  "name_pattern");
    mFWNImgsPerFile = mParams.create(EigFWNImgsPerFileStr, asynParamInt32, SSFWConfig,  "nimages_per_file");
    mFWImgNumStart  = mParams.create(EigFWImgNumStartStr,  asynParamInt32, SSFWConfig,  "image_nr_start");
    mFWState        = mParams.create(EigFWStateStr,        asynParamOctet, SSFWStatus,  "state");
    mFWFree         = mParams.create(EigFWFreeStr,       asynParamFloat64, SSFWStatus,  "buffer_free");

    // Monitor API Parameters
    mMonitorEnable  = mParams.create(EigMonitorEnableStr,  asynParamInt32, SSMonConfig, "mode");
    mMonitorEnable->setEnumValues(modeEnum);
    mMonitorBufSize = mParams.create(EigMonitorBufSizeStr, asynParamInt32, SSMonConfig, "buffer_size");
    mMonitorState   = mParams.create(EigMonitorStateStr,   asynParamOctet, SSMonStatus, "state");

    // Stream API Parameters
    mStreamEnable     = mParams.create(EigStreamEnableStr,    asynParamInt32, SSStreamConfig, "mode");
    mStreamEnable->setEnumValues(modeEnum);
    mStreamState      = mParams.create(EigStreamStateStr,     asynParamOctet, SSStreamStatus, "state");
    mStreamDropped    = mParams.create(EigStreamDroppedStr,   asynParamInt32, SSStreamStatus, "dropped");
    mStreamVersion    = mParams.create(EigStreamVersionStr,   asynParamInt32, SSStreamConfig, "format");

    // Base class parameters
    mAcquireTime       = mParams.create(ADAcquireTimeString,       asynParamFloat64, SSDetConfig, "count_time");
    mAcquirePeriod     = mParams.create(ADAcquirePeriodString,     asynParamFloat64, SSDetConfig, "frame_time");
    mNumImages         = mParams.create(ADNumImagesString,         asynParamInt32,   SSDetConfig, "nimages");
    mTriggerMode       = mParams.create(ADTriggerModeString,       asynParamInt32,   SSDetConfig, "trigger_mode");

    // Map Trigger Mode ordering
    vector<string> triggerModeEnum;
    triggerModeEnum.resize(6);
    triggerModeEnum[TRIGGER_MODE_INTS] = "ints";
    triggerModeEnum[TRIGGER_MODE_INTE] = "inte";
    triggerModeEnum[TRIGGER_MODE_EXTS] = "exts";
    triggerModeEnum[TRIGGER_MODE_EXTE] = "exte";
    triggerModeEnum[TRIGGER_MODE_CONTINUOUS] = "ints";
#ifdef HAVE_EXTG_FIRMWARE
    if (mEigerModel == Eiger2 || mEigerModel == Pilatus4) {
        triggerModeEnum[TRIGGER_MODE_EXTG] = "extg";
    }
#endif
    mTriggerMode->setEnumValues(triggerModeEnum);
    mSDKVersion        = mParams.create(ADSDKVersionString,        asynParamOctet,   SSDetConfig, "software_version");
    mFirmwareVersion   = mParams.create(ADFirmwareVersionString,   asynParamOctet,   SSDetConfig, "eiger_fw_version");
    mSerialNumber      = mParams.create(ADSerialNumberString,      asynParamOctet,   SSDetConfig, "detector_number");
    mTemperatureActual = mParams.create(ADTemperatureActualString, asynParamFloat64, SSDetStatus, "board_000/th0_temp");
    mNDArraySizeX      = mParams.create(NDArraySizeXString,        asynParamInt32,   SSDetConfig, "x_pixels_in_detector");
    mNDArraySizeY      = mParams.create(NDArraySizeYString,        asynParamInt32,   SSDetConfig, "y_pixels_in_detector");

    if (mAPIVersion == API_1_6_0)
    {
        // Work around missing 'allowed_values'
        vector<string> linkEnum;
        linkEnum.reserve(2);
        linkEnum.push_back("down");
        linkEnum.push_back("up");
        mLink0 = mParams.create(EigLink0Str, asynParamInt32, SSDetStatus, "link_0");
        mLink1 = mParams.create(EigLink1Str, asynParamInt32, SSDetStatus, "link_1");
        mLink2 = mParams.create(EigLink2Str, asynParamInt32, SSDetStatus, "link_2");
        mLink3 = mParams.create(EigLink3Str, asynParamInt32, SSDetStatus, "link_3");
        for(int i = mLink0->getIndex(); i <= mLink3->getIndex(); ++i)
        {
            mParams.getByIndex(i)->setEnumValues(linkEnum);
        }
        mDCUBufFree = mParams.create(EigDCUBufFreeStr, asynParamFloat64, SSDetStatus, "builder/dcu_buffer_free");
        mFWClear = mParams.create(EigFWClearStr, asynParamInt32, SSFWCommand, "clear");
    }
    else if (mEigerModel == Eiger2 || mEigerModel == Pilatus4)
    {
        mThreshold1Enable    = mParams.create(EigThreshold1EnableStr,    asynParamInt32,   SSDetConfig, "threshold/1/mode");
        mThreshold1Enable->setEnumValues(modeEnum);
        mTriggerStartDelay   = mParams.create(EigTriggerStartDelayStr,   asynParamFloat64, SSDetConfig, "trigger_start_delay");
        mThreshold2          = mParams.create(EigThreshold2Str,          asynParamFloat64, SSDetConfig, "threshold/2/energy");
        mThreshold2->setEpsilon(ENERGY_EPSILON);
        mThreshold2Enable    = mParams.create(EigThreshold2EnableStr,    asynParamInt32,   SSDetConfig, "threshold/2/mode");
        mThreshold2Enable->setEnumValues(modeEnum);
        mThresholdDiffEnable = mParams.create(EigThresholdDiffEnableStr, asynParamInt32,   SSDetConfig, "threshold/difference/mode");
        mThresholdDiffEnable->setEnumValues(modeEnum);
        // HV parameters
        mHVState             = mParams.create(EigHVStateStr,             asynParamOctet,   SSDetStatus, "high_voltage/state");
        mHVResetTime         = mParams.create(EigHVResetTimeStr,         asynParamFloat64);
        mHVReset             = mParams.create(EigHVResetStr,             asynParamInt32);
        mFWHDF5Format        = mParams.create(EigFWHD5FormatStr,         asynParamInt32,   SSFWConfig,  "format");
#ifdef HAVE_EXTG_FIRMWARE
        mExtGateMode         = mParams.create(EigExtGateModeStr,         asynParamInt32,   SSDetConfig, "extg_mode");
        mNumExposures        = mParams.create(ADNumExposuresString,      asynParamInt32,   SSDetConfig, "nexpi");
#endif
    }

    if (mEigerModel == Pilatus4)
    {
        mThreshold3          = mParams.create(EigThreshold3Str,          asynParamFloat64, SSDetConfig, "threshold/3/energy");
        mThreshold3->setEpsilon(ENERGY_EPSILON);
        mThreshold3Enable    = mParams.create(EigThreshold3EnableStr,    asynParamInt32,   SSDetConfig, "threshold/3/mode");
        mThreshold3Enable->setEnumValues(modeEnum);
        mThreshold4          = mParams.create(EigThreshold4Str,          asynParamFloat64, SSDetConfig, "threshold/4/energy");
        mThreshold4->setEpsilon(ENERGY_EPSILON);
        mThreshold4Enable    = mParams.create(EigThreshold4EnableStr,    asynParamInt32,   SSDetConfig, "threshold/4/mode");
        mThreshold4Enable->setEnumValues(modeEnum);
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

    status |= (epicsThreadCreate("eigerInitializeTask", epicsThreadPriorityHigh,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)initializeTaskC, this) == NULL);

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
    EigerParam *p;

    int adStatus;
    bool armed;
    getIntegerParam(ADStatus, &adStatus);
    mArmed->get(armed);

    if (function == ADAcquire)
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
            setIntegerParam(ADStatus, ADStatusAborted);
            unlock();
            mApi.abort();
            lock();
            mStopEvent.signal();
        }
        setIntegerParam(ADAcquire, value);
    }
    else if (mAPIVersion == API_1_6_0 && function == mFWClear->getIndex())
    {
        status = (asynStatus) mFWClear->put(1);
        mFWFree->fetch();
    }
    else if (function == ADReadStatus)
        status = eigerStatus();
    else if (function == mInitialize->getIndex() && value == 1)
    {
        setIntegerParam(mInitialize->getIndex(), 1);
        mInitializeEvent.signal();
    }
    else if (function == mTrigger->getIndex())
        mTriggerEvent.signal();
    else if (function == mFilePerms->getIndex())
        status = (asynStatus) mFilePerms->put(value & 0666);
    else if ((mEigerModel == Eiger2 || mEigerModel == Pilatus4) && (function == mHVReset->getIndex())) {
        double resetTime;
        mHVResetTime->get(resetTime);
        mApi.hvReset((int)resetTime);
    }
    else if (function == mTriggerMode->getIndex()) {
        if (value == TRIGGER_MODE_INTE || value == TRIGGER_MODE_EXTE) {
            mNumImages->put(1);
        }
        status = (asynStatus) mTriggerMode->put(value);
    }
    else if ((p = mParams.getByIndex(function))) {
        status = (asynStatus) p->put(value);
        if ((p == mDataSource) || (p ==mStreamVersion)) {
            int dataSource;
            mDataSource->get(dataSource);
            if (dataSource == SOURCE_STREAM) {
                // When switching DataSource to stream we need to create a StreamAPI object if it does not exist
                int streamVersion;
                mStreamVersion->get(streamVersion);
                if (streamVersion == STREAM_VERSION_STREAM) {
                    if (!mStreamAPI) {
                        mStreamAPI = new StreamAPI(mHostname);
                    }
                 } else {
                    if (!mStream2API) {
                          mStream2API = new Stream2API(mHostname);;
                    }
                }
                // It also seems to be necessary to disable and enable stream
                mStreamEnable->put(0);
                mStreamEnable->put(1);
            } else {
                // When switching DataSource to anything other than stream we need to delete the StreamAPI or Stream2API object
                // if it exists, so that we are no longer receiving zmq messages.
                // This allows other clients to receive all messages from the zmq stream.
                if (mStreamAPI) {
                    delete mStreamAPI;
                    mStreamAPI = 0;
                }
                if (mStream2API) {
                    delete mStream2API;
                    mStream2API = 0;
                }
            }
        }
    }
    else if(function < mFirstParam)
        status = ADDriver::writeInt32(pasynUser, value);

    if(status)
    {
        ERR_ARGS("error status=%d function=%d, value=%d", status, function, value);
        return status;
    }

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

    EigerParam *p;
    if (function == mPhotonEnergy->getIndex())
    {
        setStringParam(ADStatusMessage, "Setting Photon Energy...");
        callParamCallbacks();
        mPhotonEnergy->put(value);
        setStringParam(ADStatusMessage, "Photon Energy set");
    }
    else if (function == mThreshold->getIndex())
    {
        setStringParam(ADStatusMessage, "Setting Threshold Energy...");
        callParamCallbacks();
        mThreshold->put(value);
        setStringParam(ADStatusMessage, "Threshold Energy set");
    }
    else if (function == mWavelength->getIndex())
    {
        setStringParam(ADStatusMessage, "Setting Wavelength...");
        callParamCallbacks();
        mWavelength->put(value);
        setStringParam(ADStatusMessage, "Wavelength set");
    }
    else if (function == mWavelengthEpsilon->getIndex())
    {
        mWavelengthEpsilon->put(value);
        mWavelength->setEpsilon(value);
    }
    else if (function == mEnergyEpsilon->getIndex())
    {
        mEnergyEpsilon->put(value);
        mPhotonEnergy->setEpsilon(value);
        mThreshold->setEpsilon(value);
        if (mEigerModel == Eiger2 || mEigerModel == Pilatus4)
            mThreshold2->setEpsilon(value);
        if (mEigerModel == Pilatus4)
        {
            mThreshold3->setEpsilon(value);
            mThreshold4->setEpsilon(value);
        }
    }
    else if((p = mParams.getByIndex(function)))
        status = (asynStatus) p->put(value);
    else if(function < mFirstParam)
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
    EigerParam *p;

    if (function == mFileOwner->getIndex())
    {
        if(!strlen(value))
        {
            mFsUid = getuid();
            mFileOwner->put(getpwuid(mFsUid)->pw_name);
        }
        else
        {
            struct passwd *pwd = getpwnam(value);

            if(pwd) {
                mFsUid = pwd->pw_uid;
                mFileOwner->put(value);
            } else {
                ERR_ARGS("couldn't get uid for user '%s'", value);
                status = asynError;
            }
        }
    }
    else if (function == mFileOwnerGroup->getIndex())
    {
        if(!strlen(value))
        {
            mFsGid = getgid();
            mFileOwnerGroup->put(getgrgid(mFsGid)->gr_name);
        }
        else
        {
            struct group *grp = getgrnam(value);

            if(grp) {
                mFsGid = grp->gr_gid;
                mFileOwnerGroup->put(value);
            } else {
                ERR_ARGS("couldn't get gid for group '%s'", value);
                status = asynError;
            }
        }
    }
    else if((p = mParams.getByIndex(function))) {
        status = (asynStatus) p->put(value);
    }
    else if (function < mFirstParam) {
        status = ADDriver::writeOctet(pasynUser, value, nChars, nActual);
    }
    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "%s:%s: status=%d, function=%d, value=%s",
                  driverName, functionName, status, function, value);
    else
    {
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s:%s: function=%d, value=%s\n",
              driverName, functionName, function, value);
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
    fprintf(fp, "Eiger detector %s, model=%d, API=%d\n", this->portName, mEigerModel, mAPIVersion);
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
    const char *functionName = "controlTask";

    int status = asynSuccess;
    string compressionAlgo;
    int triggerMode;
    bool fwEnable, streamEnable, manualTrigger, compression, removeFiles;
    int dataSource, adStatus;
    int sequenceId, saveFiles, numImages, numTriggers;
    int numImagesPerFile;
    double acquirePeriod, triggerStartDelay, triggerTimeout = 0.0, triggerExposure = 0.0;
    int savedNumImages, filePerms;

    lock();

    for(;;)
    {
        // Wait for start event
        getIntegerParam(ADStatus, &adStatus);
        if(adStatus == ADStatusIdle)
            setStringParam(ADStatusMessage, "Ready");
        callParamCallbacks();

        unlock();
        mStartEvent.wait();
        lock();

        setIntegerParam(ADStatus, ADStatusAcquire);

        // Clear uncaught events
        mStopEvent.tryWait();
        mTriggerEvent.tryWait();
        mPollDoneEvent.tryWait();
        mStreamEvent.tryWait();

        // Latch parameters
        mDataSource->get(dataSource);
        mFWEnable->get(fwEnable);
        mStreamEnable->get(streamEnable);
        mSaveFiles->get(saveFiles);
        mFWNImgsPerFile->get(numImagesPerFile);
        mAcquirePeriod->get(acquirePeriod);
        mNumImages->get(numImages);
        mNTriggers->get(numTriggers);
        getIntegerParam(ADTriggerMode, &triggerMode);
        mManualTrigger->get(manualTrigger);
        mFWAutoRemove->get(removeFiles);
        mFWCompression->get(compression);
        mCompressionAlgo->get(compressionAlgo);
        mFilePerms->get(filePerms);

        const char *err = NULL;
        if(dataSource == SOURCE_FILEWRITER && !fwEnable)
            err = "FileWriter API is disabled";
        else if(dataSource == SOURCE_STREAM && !streamEnable)
            err = "Stream API is disabled";
        // If the files are encoded with bxlz4 then HDF5_PLUGIN_PATH must be set to find the decompression libraries
        // This is typically in ADSupport/lib/linux-x86_64 or ADSupport/bin/windows-x64.


        // If saving files, check if the File Path is valid
        if(fwEnable && saveFiles)
        {
            int filePathExists;
            checkPath();
            getIntegerParam(NDFilePathExists, &filePathExists);

            if(!filePathExists)
            {
                err = "Invalid file path";
                ERR(err);
            }
        }

        if(err) {
            setIntegerParam(ADAcquire, 0);
            setIntegerParam(ADStatus, ADStatusError);
            setStringParam(ADStatusMessage, err);
            continue;
        }

        savedNumImages = numImages;
        if(triggerMode == TRIGGER_MODE_INTE || triggerMode == TRIGGER_MODE_EXTE)
        {
            numImages = 1;
            mNumImages->put(numImages);
        }

        // Arm the detector
        setStringParam(ADStatusMessage, "Arming");
        callParamCallbacks();

        unlock();
        epicsTimeStamp armStart, armEnd;
        epicsTimeGetCurrent(&armStart);
        status = mApi.arm(&sequenceId);
        epicsTimeGetCurrent(&armEnd);
        FLOW_ARGS("arming time %f", epicsTimeDiffInSeconds(&armEnd, &armStart));
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
        setIntegerParam(ADNumImagesCounter, 0);
        setStringParam (ADStatusMessage, "Armed");
        mSequenceId->put(sequenceId);
        mArmed->put(true);
        callParamCallbacks();

        mFrameNumber = 0;
        bool waitPoll = false, waitStream = false;

        // Start FileWriter thread
        if(dataSource == SOURCE_FILEWRITER || (fwEnable && saveFiles))
        {
            acquisition_t acq;
            
            string acq_pattern_temp;
            mFWNamePattern->get(acq_pattern_temp);
            strncpy(acq.pattern, acq_pattern_temp.c_str(), sizeof(acq.pattern));
            // Add null terminator to end of pattern string in case buffer is not large enough
            acq.pattern[MAX_BUF_SIZE - 1] = '\0';
            
            acq.sequenceId  = sequenceId;
            acq.nDataFiles  = ceil(((double)(numImages*numTriggers))/((double)numImagesPerFile));
            acq.saveFiles   = saveFiles;
            acq.parseFiles  = dataSource == SOURCE_FILEWRITER;
            acq.removeFiles = removeFiles;
            acq.filePerms   = (mode_t) filePerms;

            mPollComplete = false;
            mPollStop = false;
            mPollQueue.send(&acq, sizeof(acq));
            waitPoll = true;
        }

        // Start Stream thread
        if(dataSource == SOURCE_STREAM)
        {
            mStreamComplete = false;
            mStreamEvent.signal();
            waitStream = true;
        }

        // Trigger
        if(triggerMode == TRIGGER_MODE_EXTS || triggerMode == TRIGGER_MODE_EXTE)
            setStringParam(ADStatusMessage, "Waiting for external triggers (press Stop when done)");
        else if(manualTrigger)
            setStringParam(ADStatusMessage, "Waiting for manual triggers");
        else
            setStringParam(ADStatusMessage, "Triggering");
        callParamCallbacks();

        if(triggerMode == TRIGGER_MODE_INTS || triggerMode == TRIGGER_MODE_INTE || triggerMode == TRIGGER_MODE_CONTINUOUS)
        {
            if(triggerMode == TRIGGER_MODE_INTS || triggerMode == TRIGGER_MODE_CONTINUOUS)
            {
                triggerTimeout  = acquirePeriod*numImages + 10.0;
                if (mEigerModel == Eiger2 || mEigerModel == Pilatus4) // Should this depend on the model or the API?
                {
                    mTriggerStartDelay->get(triggerStartDelay);
                    triggerTimeout += triggerStartDelay;
                }
                triggerExposure = 0.0;
            }

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
                }

                // triggerExposure might have changed
                if(triggerMode == TRIGGER_MODE_EXTE)
                {
                    mTriggerExp->get(triggerExposure);
                    triggerTimeout = triggerExposure + 1.0;
                }

                if(doTrigger)
                {
                    FLOW_ARGS("sending trigger %d/%d. timeout=%.6f, exposure=%.6f",
                            triggers+1, numTriggers, triggerTimeout, triggerExposure);
                    setShutter(1);
                    unlock();
                    status = mApi.trigger(triggerTimeout, triggerExposure);
                    lock();
                    setShutter(0);
                    ++triggers;
                }

                getIntegerParam(ADStatus, &adStatus);
            }
        }
        else // TMExternalSeries or TMExternalEnable
        {
            // The Eiger does not indicate when acquisition is complete.
            // Wait either until the NumImagesCounter is the expected value or
            // until there is a manual stop event.
            int expectedImages = numImages * numTriggers;
            int numImagesCounter;
            for(;;)
            {
                getIntegerParam(ADNumImagesCounter, &numImagesCounter);
                if (numImagesCounter >= expectedImages) break;
                if (mStopEvent.tryWait()) break;
                unlock();
                epicsThreadSleep(0.1);
                lock();
            }
        }

        // All triggers issued, disarm the detector
        unlock();
        status = mApi.disarm();
        lock();

        // Wait for tasks completion
        mArmed->put(false);
        setStringParam(ADStatusMessage, "Processing files");
        callParamCallbacks();

        bool success = true;
        unlock();
        if(waitPoll)
        {
            // Wait FileWriter to go out of the "acquire" state
            FLOW("waiting for FileWriter");
            string fwAcquire;
            do
            {
                mFWState->get(fwAcquire);
            }while(fwAcquire == "acquire");
            epicsThreadSleep(0.5);

            // Request polling task to stop
            mPollStop = true;

            FLOW("waiting for pollTask");
            mPollDoneEvent.wait();
            success = success && mPollComplete;
            FLOW_ARGS("pollTask complete = %d", mPollComplete);
        }

        if(waitStream)
        {
            FLOW("waiting for streamTask");
            mStreamDoneEvent.wait();
            success = success && mStreamComplete;
            FLOW_ARGS("streamTask complete = %d", mStreamComplete);
        }
        lock();

        if(savedNumImages != numImages)
            mNumImages->put(savedNumImages);

        getIntegerParam(ADStatus, &adStatus);
        if(adStatus == ADStatusAcquire) {
            if (triggerMode == TRIGGER_MODE_CONTINUOUS) {
                mStartEvent.signal();
            } else {
                setIntegerParam(ADStatus, ADStatusIdle);
                setIntegerParam(ADAcquire, 0);
            }         
        } else if(adStatus == ADStatusAborted) {
            setStringParam(ADStatusMessage, "Acquisition aborted");
            setIntegerParam(ADAcquire, 0);
        }
        callParamCallbacks();
    }
}

void eigerDetector::pollTask (void)
{
    const size_t MAX_RETRIES = 2;
    const char *functionName = "pollTask";
    acquisition_t acquisition;
    int pendingFiles;
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
            files[i].uid      = mFsUid;
            files[i].gid      = mFsGid;
            files[i].perms    = acquisition.filePerms;

            if(isMaster)
                RestAPI::buildMasterName(acquisition.pattern, acquisition.sequenceId,
                        files[i].name, sizeof(files[i].name));
            else
                RestAPI::buildDataName(i-1+DEFAULT_NR_START, acquisition.pattern,
                        acquisition.sequenceId, files[i].name,
                        sizeof(files[i].name));
        }

        // While acquiring, wait and download every file on the list
        lock();
        mPendingFiles->put(0);
        unlock();

        i = 0;
        size_t retries = 0;
        while(i < totalFiles && retries <= MAX_RETRIES)
        {
            file_t *curFile = &files[i];

            FLOW_ARGS("file=%s", curFile->name);
            if(!mApi.waitFile(curFile->name, 1.0))
            {
                FLOW_ARGS("file=%s exists", curFile->name);
                if(curFile->save || curFile->parse)
                {
                    lock();
                    mPendingFiles->get(pendingFiles);
                    mPendingFiles->put(pendingFiles+1);
                    unlock();

                    mDownloadQueue.send(&curFile, sizeof(curFile));
                }
                else if(curFile->remove)
                    mApi.deleteFile(curFile->name);
                ++i;
            }
            // pollTask was asked to stop and it failed to find a pending file
            // (acquisition aborted), so let's retry
            else if(mPollStop)
            {
                FLOW_ARGS("file=%s not found and pollTask asked to stop",
                        curFile->name);
                ++retries;
            }
        }

        // Not acquiring anymore, wait for all pending files to be reaped
        FLOW("waiting for pending files");
        do
        {
            lock();
            mPendingFiles->get(pendingFiles);
            unlock();

            epicsThreadSleep(0.1);
        }while(pendingFiles);
        FLOW("done waiting for pending files");

        // All pending files were processed and reaped
        free(files);
        mPollComplete = i == totalFiles;
        mPollDoneEvent.signal();
    }
}

void eigerDetector::downloadTask (void)
{
    const char *functionName = "downloadTask";
    file_t *file;

    for(;;)
    {
        mDownloadQueue.receive(&file, sizeof(file_t *));

        FLOW_ARGS("file=%s", file->name);

        file->refCount = file->parse + file->save;

        // Download the file
        if(mApi.getFile(file->name, &file->data, &file->len))
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
    uid_t currentFsUid = getuid();
    uid_t currentFsGid = getgid();

    for(;;)
    {
        int fd;
        ssize_t written = 0;
        size_t total_written = 0;

        mSaveQueue.receive(&file, sizeof(file_t *));

        FLOW_ARGS("file=%s uid=%d gid=%d", file->name, file->uid, file->gid);

        if(file->uid != currentFsUid)
        {
            FLOW_ARGS("setting FS UID to %d", file->uid);
            setfsuid(file->uid);
            currentFsUid = (uid_t)setfsuid(file->uid);

            if(currentFsUid != file->uid)
                ERR_ARGS("[file=%s] failed to set uid", file->name);

        }

        if(file->gid != currentFsGid)
        {
            FLOW_ARGS("setting FS GID to %d", file->gid);
            setfsgid(file->gid);
            currentFsGid = (uid_t)setfsgid(file->gid);

            if(currentFsGid != file->gid)
                ERR_ARGS("[file=%s] failed to set gid", file->name);

        }

        lock();
        setStringParam(NDFileName, file->name);
        setStringParam(NDFileTemplate, "%s%s");
        createFileName(sizeof(fullFileName), fullFileName);
        setStringParam(NDFullFileName, fullFileName);
        callParamCallbacks();
        unlock();

        fd = open(fullFileName, O_WRONLY | O_CREAT, file->perms);
        if(fd < 0)
        {
            ERR_ARGS("[file=%s] unable to open file to be written\n[%s]",
                    file->name, fullFileName);
            perror("open");
            file->remove = false;
            goto reap;
        }

        if(fchmod(fd, file->perms) < 0)
        {
            ERR_ARGS("[file=%s] failed to set permissions %o", file->name,
                    file->perms);
            perror("fchmod");
        }

        total_written = 0;
        while(total_written < file->len)
        {
            written = write(fd, file->data + total_written, file->len - total_written);
            if(written <= 0)
            {
                ERR_ARGS("[file=%s] failed to write to local file (%lu written)",
                         file->name, total_written);
                perror("write");
                file->remove = false;
                break;
            }
            total_written += written;
        }
        close(fd);

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
            if(file->remove)
                mApi.deleteFile(file->name);

            mFWFree->fetch();

            if(file->data)
            {
                free(file->data);
                file->data = NULL;
                FLOW_ARGS("file=%s reaped", file->name);
            }

            lock();
            mPendingFiles->get(pendingFiles);
            mPendingFiles->put(pendingFiles-1);
            unlock();
        }
    }
}

void eigerDetector::monitorTask (void)
{
    const char *functionName = "monitorTask";

    for(;;)
    {
        bool enabled;
        int timeout;

        lock();
        mMonitorEnable->get(enabled);
        mMonitorTimeout->get(timeout);
        unlock();

        if(enabled)
        {
            char *buf = NULL;
            size_t bufSize;

            if(!mApi.getMonitorImage(&buf, &bufSize, (size_t) timeout))
            {
                if(parseTiffFile(buf, bufSize))
                    ERR("couldn't parse file");

                free(buf);
            }
        }

        epicsThreadSleep(0.1); // Rate limit to 10Hz
    }
}

void eigerDetector::streamTask (void)
{
    const char *functionName = "streamTask";

    lock();
    for(;;)
    {
        unlock();
        mStreamEvent.wait();
        lock();

        int streamVersion;
        mStreamVersion->get(streamVersion);
        
       if (((streamVersion == STREAM_VERSION_STREAM) && !mStreamAPI) ||
           ((streamVersion == STREAM_VERSION_STREAM2) && !mStream2API)) {
            ERR("mStreamAPI is null, Stream API not enabled?");
            continue;
        }
        int err;
        stream_header_t header = {};
        for(;;)
        {
            unlock();
            if (streamVersion == STREAM_VERSION_STREAM) {
                err = mStreamAPI->getHeader(&header, 1);
            } else {
                err = mStream2API->getHeader(&header, 1);
            }
            lock();
            if ( err == STREAM_SUCCESS) {
                break;
            } else if (err == STREAM_WRONG_HTYPE) {
                ERR("got stray packet, ignoring");
                continue;
            } else if (err == STREAM_ERROR) {
                ERR("failed to get header packet");
                goto end;
            } else if (err == STREAM_TIMEOUT) {
                // The following code jumps to the end of the outer loop if acquisition has been aborted
                // Doing so will leave the ZMQ socket with stale messages, which will corrupt the next acquisition.
                // We fix this by closing and reopening the ZMQ socket
                // However, this code does not appear to be necessary, because the Eiger reliably sends frame.end()
                // when acquisition is aborted.  We leave it commented out in case it proves to be needed in the future
                /*
                if(!acquiring())
                {
                    // This means acquisition was stopped during a series
                    // We need to either wait for all ZMQ data that is pending or close and re-open the socket.
                    delete mStreamAPI;
                    mStreamAPI = new StreamAPI(mHostname);
                    goto end;
                }
                */
                FLOW("got stream timeout");
                continue;
            } else {
                ERR("unknown err from mStreamAPI->getHeader()");
                goto end;
            }
        }

        for(;;)
        {
            int endFrames;
             for(;;)
            {
               unlock();
               if (streamVersion == STREAM_VERSION_STREAM) {
                    err = mStreamAPI->waitFrame(&endFrames);
                } else {
                    err = mStream2API->waitFrame(&endFrames);
                }
                lock();
                if (err == STREAM_SUCCESS) {
                    break;
                } else if (err == STREAM_ERROR) {
                    ERR("failed to get frame packet");
                    goto end;
                } else if (err == STREAM_TIMEOUT) {
                    FLOW("got stream timeout");
                    continue;
                } else {
                    ERR("unknown err from mStreamAPI->waitFrame()");
                    goto end;
                }
            }

            if(endFrames)
            {
                FLOW("got end frame");
                mStreamComplete = true;
                break;
            }

            NDArray *pArray;
            int decompress;
            mStreamDecompress->get(decompress);
            if (streamVersion == STREAM_VERSION_STREAM) {
                err = mStreamAPI->getFrame(&pArray, pNDArrayPool, decompress);
            } else {
                err = mStream2API->getFrame(&pArray, pNDArrayPool, decompress);
            }
            int imageCounter, numImagesCounter, arrayCallbacks;
            getIntegerParam(NDArrayCounter, &imageCounter);
            getIntegerParam(ADNumImagesCounter, &numImagesCounter);
            getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
            
            // The data returned from the StreamAPIs is unsigned.
            // Bad pixels and gaps are very large positive numbers, which makes autoscaling difficult
            // Optionally change the data type to signed. 
            // This improves autoscaling, but reduces the count range by 2X.
            int signedData;
            mSignedData->get(signedData);
            if (signedData) {
                int dataType = pArray->dataType;
                switch (pArray->dataType) {
                    case NDUInt8:
                        pArray->dataType = NDInt8;
                        break;
                    case NDUInt16:
                        pArray->dataType = NDInt16;
                        break;
                    case NDUInt32:
                        pArray->dataType = NDInt32;
                        break;
                    default:
                        ERR_ARGS("Unknown data type=%d", dataType);
                }
            }

            // Put the frame number and timestamp into the buffer
            pArray->uniqueId = imageCounter;

            updateTimeStamps(pArray);

            // Update Omega angle for this frame
            ++mFrameNumber;

            // Get any attributes that have been defined for this driver
            this->getAttributes(pArray->pAttributeList);

            // Call the NDArray callback
            if (arrayCallbacks)
                doCallbacksGenericPointer(pArray, NDArrayData, 0);

            setIntegerParam(NDArrayCounter, ++imageCounter);
            setIntegerParam(ADNumImagesCounter, ++numImagesCounter);

            callParamCallbacks();
            pArray->release();
        }

end:
        mStreamDropped->fetch();

        mStreamDoneEvent.signal();
    }
}

void eigerDetector::initializeTask()
{
    const char *functionName = "initializeTask";
    for(;;)
    {
        mInitializeEvent.wait();

        asynPrint(this->pasynUserSelf, ASYN_TRACE_WARNING,
                  "%s:%s: Sending initialize command\n",
                  driverName, functionName);

        int status = mApi.initialize();

        lock();
        setIntegerParam(mInitialize->getIndex(), 0);
        unlock();

        if (status) {
            ERR("Failed to initialize");
        }

        // Clear events
        mInitializeEvent.tryWait();
    }
}

asynStatus eigerDetector::initParams (void)
{
    int status = asynSuccess;

    mParams.fetchAll();

    // Get the sensor size without ROI
    string roiMode;
    int maxSizeX, maxSizeY;
    mROIMode->get(roiMode);

    if(roiMode != "disabled")
        mROIMode->put("disabled");

    mNDArraySizeX->get(maxSizeX);
    mNDArraySizeY->get(maxSizeY);

    if(roiMode != "disabled")
        mROIMode->put(roiMode);

    setIntegerParam(ADMaxSizeX, maxSizeX);
    setIntegerParam(ADMaxSizeY, maxSizeY);

    string description;
    status |= mDescription->get(description);

    size_t space = description.find(' ');
    string manufacturer(description, 0, space);
    string model(description, space+1);

    status |= setStringParam (ADManufacturer, manufacturer);
    status |= setStringParam (ADModel, model);

    // Set some default values
    status |= setIntegerParam(NDArraySize, 0);
    status |= setIntegerParam(NDDataType,  NDUInt32);
    status |= setIntegerParam(ADImageMode, ADImageMultiple);

    mArmed->put(false);
    mSequenceId->put(0);
    mPendingFiles->put(0);
    mMonitorEnable->put(false);
    mMonitorTimeout->put(500);
    mFileOwner->put("");
    mFileOwnerGroup->put("");
    mFilePerms->put(0644);

    // Auto Summation should always be true (SIMPLON API Reference v1.3.0)
    mAutoSummation->put(true);

    // This driver expects the following parameters to always have the same value
    mFWImgNumStart->put(DEFAULT_NR_START);
    mMonitorBufSize->put(1);

    callParamCallbacks();

    return (asynStatus)status;
}

asynStatus eigerDetector::parseH5File (char *buf, size_t bufLen)
{
    const char *functionName = "parseH5File";
    asynStatus status = asynSuccess;

    int imageCounter, numImagesCounter, arrayCallbacks;
    hid_t fId, dId, dSpace, dType, mSpace;
    hsize_t dims[3], count[3], offset[3] = {0,0,0};
    herr_t err;
    size_t nImages, width, height;

    size_t ndDims[2];
    NDDataType_t ndType;

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
    else if(H5Tequal(dType, H5T_NATIVE_UINT8) > 0)
        ndType = NDUInt8;
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
    getIntegerParam(ADNumImagesCounter, &numImagesCounter);
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
        updateTimeStamps(pImage);

        // Update the omega angle for this frame
        ++mFrameNumber;

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
        setIntegerParam(ADNumImagesCounter, ++numImagesCounter);
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
    static int uniqueId = 1;
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

    pImage->uniqueId = uniqueId++;
    updateTimeStamps(pImage);

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
    // If we are acquiring return immediately
    int acquiring;
    getIntegerParam(ADAcquire, &acquiring);
    if (acquiring)
        return asynSuccess;

    // Request a status update
    if (mAPIVersion == API_1_6_0)
    {
        if(mApi.statusUpdate())
            return asynError;
    }

    int status = 0;
    // Read state and error message
    status |= mState->fetch();
    status |= mError->fetch();

    // Read temperature and humidity
    status |= mThTemp0->fetch();
    status |= mTemperatureActual->fetch();
    status |= mThHumid0->fetch();

    // Read a few more interesting parameters
    if (mAPIVersion == API_1_6_0)
    {
        // Read the status of each individual link between the head and the server
        status |= mLink0->fetch();
        status |= mLink1->fetch();
        std::string model;
        getStringParam(ADModel, model);
        // The Eiger 500K does not have link2 or link3
        if (model.find("500K") == std::string::npos) {
            status |= mLink2->fetch();
            status |= mLink3->fetch();
        }
        // Read DCU buffer free percentage
        status |= mDCUBufFree->fetch();
    }
    if (mEigerModel == Eiger2 || mEigerModel == Pilatus4)
    {
        status |= mHVState->fetch();
    }

    // Read state of the different modules
    status |= mFWState->fetch();
    status |= mMonitorState->fetch();
    status |= mStreamState->fetch();

    // Read a few more interesting parameters
    status |= mStreamDropped->fetch();
    status |= mFWFree->fetch();

    callParamCallbacks();
    return status==0 ? asynSuccess : asynError;
}

bool eigerDetector::acquiring (void)
{
    int adStatus;
    lock();
    getIntegerParam(ADStatus, &adStatus);
    unlock();
    return adStatus == ADStatusAcquire;
}

asynStatus eigerDetector::drvUserCreate(asynUser *pasynUser, const char *drvInfo,
        const char **pptypeName, size_t *psize) {
    const char *functionName = "drvUserCreate";
    /*printf("drvUserCreate(pasynUser=%p, drvInfo=%s, pptypeName=%p, psize=%p)\n",
            pasynUser, drvInfo, pptypeName, psize);*/
    int index;

    if(findParam(drvInfo, &index) && strlen(drvInfo) > 8 && !strncmp(drvInfo, "EIG_", 4))
    {
        /* Parameters are of the format
         *  EIG_XYZ_name
         *
         * Where:
         *   X is one of 'D': Detector
         *               'F': FileWriter
         *               'M': Monitor
         *               'S': Stream
         *
         *   Y is one of 'C': Config
         *               'S': Status
         *
         *   Z is one of 'I': asynInt32
         *               'D': asynFloat64
         *               'S': asynOctet
         */

        string subSystemStr(drvInfo+4, 2);
        map<string, sys_t>::const_iterator subSystemIt;
        asynParamType asynType;

        subSystemIt = mSubSystemMap.find(subSystemStr);

        if(subSystemIt == mSubSystemMap.end())
        {
            ERR_ARGS("[%s] couldn't match %s to any subsystem", drvInfo,
                    subSystemStr.c_str());
            return asynError;
        }

        switch(drvInfo[6])
        {
        case 'I':   asynType = asynParamInt32;      break;
        case 'D':   asynType = asynParamFloat64;    break;
        case 'S':   asynType = asynParamOctet;      break;
        default:
            ERR_ARGS("[%s] couldn't match %c to an asyn type", drvInfo,  drvInfo[6]);
            return asynError;
        }

        string paramName(drvInfo+8);

        EigerParam *p = mParams.create(drvInfo, asynType, subSystemIt->second, paramName);
        if(!p)
            return asynError;

        p->fetch();
    }
    return ADDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
}

extern "C" int eigerDetectorConfig(const char *portName, const char *serverPort,
                                   int maxBuffers, size_t maxMemory, int priority, int stackSize)
{
    new eigerDetector(portName, serverPort, maxBuffers, maxMemory, priority, stackSize);
    return asynSuccess;
}

// Code for iocsh registration
static const iocshArg eigerDetectorConfigArg0 = {"Port name", iocshArgString};
static const iocshArg eigerDetectorConfigArg1 = {"Server host name", iocshArgString};
static const iocshArg eigerDetectorConfigArg2 = {"maxBuffers", iocshArgInt};
static const iocshArg eigerDetectorConfigArg3 = {"maxMemory", iocshArgInt};
static const iocshArg eigerDetectorConfigArg4 = {"priority", iocshArgInt};
static const iocshArg eigerDetectorConfigArg5 = {"stackSize", iocshArgInt};
static const iocshArg * const eigerDetectorConfigArgs[] = {
    &eigerDetectorConfigArg0, &eigerDetectorConfigArg1, &eigerDetectorConfigArg2,
    &eigerDetectorConfigArg3, &eigerDetectorConfigArg4, &eigerDetectorConfigArg5};

static const iocshFuncDef configeigerDetector = {"eigerDetectorConfig", 6, eigerDetectorConfigArgs};

static void configeigerDetectorCallFunc(const iocshArgBuf *args)
{
    eigerDetectorConfig(args[0].sval, args[1].sval, args[2].ival,
                        args[3].ival, args[4].ival, args[5].ival);
}

static void eigerDetectorRegister(void)
{
    iocshRegister(&configeigerDetector, configeigerDetectorCallFunc);
}

extern "C" {
    epicsExportRegistrar(eigerDetectorRegister);
}

