#ifndef EIGER_DETECTOR_H
#define EIGER_DETECTOR_H

#include <map>
#include <vector>

#include "restApi.h"
#include "eigerParam.h"

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
#define EigTriggerStr              "TRIGGER"
#define EigTriggerExpStr           "TRIGGER_EXPOSURE"
#define EigNTriggersStr            "NUM_TRIGGERS"
#define EigManualTriggerStr        "MANUAL_TRIGGER"
#define EigTriggerStartDelayStr    "TRIGGER_START_DELAY"
#define EigCompressionAlgoStr      "COMPRESSION_ALGO"
// ROI Mode is only available on Eiger 9M and 16M
#define EigROIModeStr              "ROI_MODE"

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

    // Eiger parameters: metadata
    EigerParam *mDescription;

    // Eiger parameters: acquisition
    EigerParam *mWavelength;
    EigerParam *mPhotonEnergy;
    EigerParam *mThreshold;
    EigerParam *mNTriggers;
    EigerParam *mCompressionAlgo;
    EigerParam *mROIMode;
    EigerParam *mAutoSummation;

    // Eiger parameters: status
    EigerParam *mState;
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

    // Eiger parameters: monitor interface
    EigerParam *mMonitorEnable;
    EigerParam *mMonitorBufSize;
    EigerParam *mMonitorState;

    // Eiger parameters: streaming interface
    EigerParam *mStreamEnable;
    EigerParam *mStreamDropped;
    EigerParam *mStreamState;

    // Base class parameters
    EigerParam *mAcquireTime;
    EigerParam *mAcquirePeriod;
    EigerParam *mNumImages;
    EigerParam *mTriggerMode;
    EigerParam *mFirmwareVersion;
    EigerParam *mSerialNumber;
    EigerParam *mTemperatureActual;
    EigerParam *mNDArraySizeX;
    EigerParam *mNDArraySizeY;

private:
    char mHostname[512];
    RestAPI mApi;
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
