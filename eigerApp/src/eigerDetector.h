#ifndef EIGER_DETECTOR_H
#define EIGER_DETECTOR_H

#include <map>
#include <vector>

#include "restApi.h"
#include "streamApi.h"
#include "eigerParam.h"

typedef enum {
  Eiger1,
  Eiger2,
  Pilatus4,
} eigerModel_t;

// areaDetector NDArray data source
#define EigDataSourceStr           "DATA_SOURCE"

// FileWriter Parameters
#define EigFWEnableStr             "FW_ENABLE"
#define EigFWClearStr              "CLEAR"
#define EigFWCompressionStr        "COMPRESSION"
#define EigFWNamePatternStr        "NAME_PATTERN"
#define EigFWNImgsPerFileStr       "NIMAGES_PER_FILE"
#define EigFWAutoRemoveStr         "AUTO_REMOVE"
#define EigFWFreeStr               "FW_FREE"
#define EigFWStateStr              "FW_STATE"
#define EigFWImgNumStartStr        "FW_IMG_NUM_START"
#define EigFWHD5FormatStr          "FWHDF5_FORMAT"

// Acquisition Metadata Parameters
#define EigWavelengthStr           "WAVELENGTH"
#define EigAutoSummationStr        "AUTO_SUMMATION"

// Detector Metadata Parameters
#define EigDescriptionStr          "DESCRIPTION"

// MX Parameters (firmware 1.6.2 onwards)
#define EigOmegaStr                "OMEGA"

// Acquisition Parameters
#define EigPhotonEnergyStr         "PHOTON_ENERGY"
#define EigThresholdStr            "THRESHOLD"
#define EigThreshold1EnableStr     "THRESHOLD1_ENABLE"
#define EigThreshold2Str           "THRESHOLD2"
#define EigThreshold2EnableStr     "THRESHOLD2_ENABLE"
#define EigThresholdDiffEnableStr  "THRESHOLD_DIFF_ENABLE"
#define EigTriggerStr              "TRIGGER"
#define EigTriggerExpStr           "TRIGGER_EXPOSURE"
#define EigNTriggersStr            "NUM_TRIGGERS"
#define EigManualTriggerStr        "MANUAL_TRIGGER"
#define EigTriggerStartDelayStr    "TRIGGER_START_DELAY"
#define EigExtGateModeStr          "EXT_GATE_MODE"
#define EigCompressionAlgoStr      "COMPRESSION_ALGO"
// ROI Mode is only available on Eiger 9M and 16M
#define EigROIModeStr              "ROI_MODE"

// Pilatus4 Parameters
#define EigThreshold3Str           "THRESHOLD3"
#define EigThreshold3EnableStr     "THRESHOLD3_ENABLE"
#define EigThreshold4Str           "THRESHOLD4"
#define EigThreshold4EnableStr     "THRESHOLD4_ENABLE"

// Detector Status Parameters
#define EigStateStr                "STATE"
#define EigErrorStr                "ERROR"
#define EigInitializeStr           "INITIALIZE"
#define EigThTemp0Str              "TH_TEMP_0"
#define EigThHumid0Str             "TH_HUMID_0"
#define EigLink0Str                "LINK_0"
#define EigLink1Str                "LINK_1"
#define EigLink2Str                "LINK_2"
#define EigLink3Str                "LINK_3"
#define EigDCUBufFreeStr           "DCU_BUF_FREE"

// Other Parameters
#define EigArmedStr                "ARMED"
#define EigSequenceIdStr           "SEQ_ID"
#define EigPendingFilesStr         "PENDING_FILES"
#define EigHVResetTimeStr          "HV_RESET_TIME"
#define EigHVResetStr              "HV_RESET"
#define EigHVStateStr              "HV_STATE"
#define EigSignedDataStr           "SIGNED_DATA"

// File Saving Parameters
#define EigSaveFilesStr            "SAVE_FILES"
#define EigFileOwnerStr            "FILE_OWNER"
#define EigFileOwnerGroupStr       "FILE_OWNER_GROUP"
#define EigFilePermsStr            "FILE_PERMISSIONS"

// Monitor API Parameters
#define EigMonitorEnableStr        "MONITOR_ENABLE"
#define EigMonitorTimeoutStr       "MONITOR_TIMEOUT"
#define EigMonitorStateStr         "MONITOR_STATE"
#define EigMonitorBufSizeStr       "MONITOR_BUF_SIZE"

// Stream API Parameters
#define EigStreamEnableStr         "STREAM_ENABLE"
#define EigStreamDroppedStr        "STREAM_DROPPED"
#define EigStreamStateStr          "STREAM_STATE"
#define EigStreamDecompressStr     "STREAM_DECOMPRESS"
#define EigStreamVersionStr        "STREAM_VERSION"

// Epsilon Parameters (minimum amount of change allowed)
#define EigWavelengthEpsilonStr    "WAVELENGTH_EPSILON"
#define EigEnergyEpsilonStr        "ENERGY_EPSILON"

//  Driver for the Dectris' Eiger pixel array detector using their REST server
class eigerDetector : public ADDriver
{
public:
    eigerDetector(const char *portName, const char *serverHostname,
                  int maxBuffers, size_t maxMemory, int priority, int stackSize);

    // These are the methods that we override from ADDriver
    virtual asynStatus writeInt32  (asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus writeOctet  (asynUser *pasynUser, const char *value,
            size_t nChars, size_t *nActual);
    void report(FILE *fp, int details);
    virtual asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo,
            const char **pptypeName, size_t *psize);

    // These should be private but are called from C so must be public
    void controlTask  (void);
    void pollTask     (void);
    void downloadTask (void);
    void parseTask    (void);
    void saveTask     (void);
    void reapTask     (void);
    void monitorTask  (void);
    void streamTask   (void);
    void initializeTask();

    enum roi_mode
    {
        ROI_MODE_DISABLED,
        ROI_MODE_4M,
    };

    enum compression_algo
    {
        COMP_ALGO_LZ4,
        COMP_ALGO_BSLZ4
    };
    
    enum trigger_mode
    {
        TRIGGER_MODE_INTS,
        TRIGGER_MODE_INTE,
        TRIGGER_MODE_EXTS,
        TRIGGER_MODE_EXTE,
        TRIGGER_MODE_CONTINUOUS,
        TRIGGER_MODE_EXTG
    };

    enum stream_version
    {
        STREAM_VERSION_STREAM,
        STREAM_VERSION_STREAM2
    };

protected:
    // Driver-only parameters
    EigerParam *mDataSource;
    EigerParam *mFWAutoRemove;
    EigerParam *mTrigger;
    EigerParam *mTriggerExp;
    EigerParam *mManualTrigger;
    EigerParam *mTriggerStartDelay;
    EigerParam *mArmed;
    EigerParam *mSequenceId;
    EigerParam *mPendingFiles;
    EigerParam *mSaveFiles;
    EigerParam *mFileOwner;
    EigerParam *mFileOwnerGroup;
    EigerParam *mFilePerms;
    EigerParam *mMonitorTimeout;
    EigerParam *mStreamDecompress;
    EigerParam *mInitialize;
    EigerParam *mHVResetTime;
    EigerParam *mHVReset;
    EigerParam *mWavelengthEpsilon;
    EigerParam *mEnergyEpsilon;
    EigerParam *mSignedData;

    // Eiger parameters: metadata
    EigerParam *mDescription;

    // Eiger parameters: acquisition
    EigerParam *mWavelength;
    EigerParam *mPhotonEnergy;
    EigerParam *mThreshold;
    EigerParam *mThreshold1Enable;
    EigerParam *mThreshold2;
    EigerParam *mThreshold2Enable;
    EigerParam *mThresholdDiffEnable;
    EigerParam *mNTriggers;
    EigerParam *mExtGateMode;
    EigerParam *mCompressionAlgo;
    EigerParam *mROIMode;
    EigerParam *mAutoSummation;

    //Pilatus4 parameters
    EigerParam *mThreshold3;
    EigerParam *mThreshold3Enable;
    EigerParam *mThreshold4;
    EigerParam *mThreshold4Enable;

    // Eiger parameters: status
    EigerParam *mState;
    EigerParam *mHVState;
    EigerParam *mError;
    EigerParam *mThTemp0;
    EigerParam *mThHumid0;
    EigerParam *mLink0;
    EigerParam *mLink1;
    EigerParam *mLink2;
    EigerParam *mLink3;
    EigerParam *mDCUBufFree;

    // Eiger parameters: filewriter interface
    EigerParam *mFWEnable;
    EigerParam *mFWCompression;
    EigerParam *mFWNamePattern;
    EigerParam *mFWNImgsPerFile;
    EigerParam *mFWImgNumStart;
    EigerParam *mFWState;
    EigerParam *mFWFree;
    EigerParam *mFWClear;
    EigerParam *mFWHDF5Format;

    // Eiger parameters: monitor interface
    EigerParam *mMonitorEnable;
    EigerParam *mMonitorBufSize;
    EigerParam *mMonitorState;

    // Eiger parameters: streaming interface
    EigerParam *mStreamEnable;
    EigerParam *mStreamDropped;
    EigerParam *mStreamState;
    EigerParam *mStreamVersion;

    // Base class parameters
    EigerParam *mAcquireTime;
    EigerParam *mAcquirePeriod;
    EigerParam *mNumImages;
    EigerParam *mNumExposures;
    EigerParam *mTriggerMode;
    EigerParam *mSDKVersion;
    EigerParam *mFirmwareVersion;
    EigerParam *mSerialNumber;
    EigerParam *mTemperatureActual;
    EigerParam *mNDArraySizeX;
    EigerParam *mNDArraySizeY;

private:
    char mHostname[512];
    RestAPI mApi;
    StreamAPI *mStreamAPI;
    Stream2API *mStream2API;
    eigerModel_t mEigerModel;
    eigerAPIVersion_t mAPIVersion;
    epicsEvent mStartEvent, mStopEvent, mTriggerEvent, mStreamEvent, mStreamDoneEvent,
            mPollDoneEvent, mInitializeEvent;
    epicsMessageQueue mPollQueue, mDownloadQueue, mParseQueue, mSaveQueue,
            mReapQueue;
    bool mPollStop, mPollComplete, mStreamComplete;
    unsigned int mFrameNumber;
    uid_t mFsUid, mFsGid;
    EigerParamSet mParams;
    int mFirstParam;
    std::map<std::string, sys_t> mSubSystemMap;

    // Read all parameters from detector and set some default values
    asynStatus initParams (void);

    // File parsers
    asynStatus parseH5File   (char *buf, size_t len);
    asynStatus parseTiffFile (char *buf, size_t len);

    // Read some detector status parameters
    asynStatus eigerStatus (void);

    // Helper that returns ADStatus == ADStatusAcquire
    bool acquiring (void);
};

#endif
