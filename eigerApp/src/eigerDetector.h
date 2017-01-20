#ifndef EIGER_DETECTOR_H
#define EIGER_DETECTOR_H

#include "restApi.h"

// areaDetector NDArray data source
#define EigerDataSourceString           "DATA_SOURCE"

// FileWriter Parameters
#define EigerFWEnableString             "FW_ENABLE"
#define EigerFWClearString              "CLEAR"
#define EigerFWCompressionString        "COMPRESSION"
#define EigerFWNamePatternString        "NAME_PATTERN"
#define EigerFWNImgsPerFileString       "NIMAGES_PER_FILE"
#define EigerFWAutoRemoveString         "AUTO_REMOVE"
#define EigerFWFreeString               "FW_FREE"

// Acquisition Metadata Parameters
#define EigerBeamXString                "BEAM_X"
#define EigerBeamYString                "BEAM_Y"
#define EigerDetDistString              "DET_DIST"
#define EigerWavelengthString           "WAVELENGTH"
#define EigerCountCutoffString          "COUNT_CUTOFF"

// Detector Metadata Parameters
#define EigerSWVersionString            "SW_VERSION"
#define EigerSerialNumberString         "SERIAL_NUMBER"
#define EigerDescriptionString          "DESCRIPTION"
#define EigerSensorThicknessString      "SENSOR_THICKNESS"
#define EigerSensorMaterialString       "SENSOR_MATERIAL"
#define EigerXPixelSizeString           "X_PIXEL_SIZE"
#define EigerYPixelSizeString           "Y_PIXEL_SIZE"

// MX Parameters (firmware 1.6.2 onwards)
#define EigerChiStartString             "CHI_START"
#define EigerChiIncrString              "CHI_INCR"
#define EigerKappaStartString           "KAPPA_START"
#define EigerKappaIncrString            "KAPPA_INCR"
#define EigerOmegaString                "OMEGA"
#define EigerOmegaStartString           "OMEGA_START"
#define EigerOmegaIncrString            "OMEGA_INCR"
#define EigerPhiStartString             "PHI_START"
#define EigerPhiIncrString              "PHI_INCR"
#define EigerTwoThetaStartString        "TWO_THETA_START"
#define EigerTwoThetaIncrString         "TWO_THETA_INCR"

// Acquisition Parameters
#define EigerFlatfieldString            "FLATFIELD_APPLIED"
#define EigerPhotonEnergyString         "PHOTON_ENERGY"
#define EigerThresholdString            "THRESHOLD"
#define EigerTriggerString              "TRIGGER"
#define EigerTriggerExpString           "TRIGGER_EXPOSURE"
#define EigerNTriggersString            "NUM_TRIGGERS"
#define EigerManualTriggerString        "MANUAL_TRIGGER"
#define EigerCompressionAlgoString      "COMPRESSION_ALGO"
// ROI Mode is only available on Eiger 9M and 16M
#define EigerROIModeString              "ROI_MODE"
#define EigerPixMaskAppliedString       "PIXEL_MASK_APPLIED"

// Detector Status Parameters
#define EigerStateString                "STATE"
#define EigerErrorString                "ERROR"
#define EigerThTemp0String              "TH_TEMP_0"
#define EigerThHumid0String             "TH_HUMID_0"
#define EigerLink0String                "LINK_0"
#define EigerLink1String                "LINK_1"
#define EigerLink2String                "LINK_2"
#define EigerLink3String                "LINK_3"
#define EigerDCUBufFreeString           "DCU_BUF_FREE"

// Other Parameters
#define EigerArmedString                "ARMED"
#define EigerSaveFilesString            "SAVE_FILES"
#define EigerSequenceIdString           "SEQ_ID"
#define EigerPendingFilesString         "PENDING_FILES"

// Monitor API Parameters
#define EigerMonitorEnableString        "MONITOR_ENABLE"
#define EigerMonitorTimeoutString       "MONITOR_TIMEOUT"

// Stream API Parameters
#define EigerStreamEnableString         "STREAM_ENABLE"
#define EigerStreamDroppedString        "STREAM_DROPPED"

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

    // These should be private but are called from C so must be public
    void controlTask  (void);
    void pollTask     (void);
    void downloadTask (void);
    void parseTask    (void);
    void saveTask     (void);
    void reapTask     (void);
    void monitorTask  (void);
    void streamTask   (void);

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
    int EigerDataSource;
    #define FIRST_EIGER_PARAM EigerDataSource
    int EigerFWEnable;
    int EigerFWClear;
    int EigerFWCompression;
    int EigerFWNamePattern;
    int EigerFWNImgsPerFile;
    int EigerFWAutoRemove;
    int EigerFWFree;
    int EigerBeamX;
    int EigerBeamY;
    int EigerDetDist;
    int EigerWavelength;
    int EigerCountCutoff;
    int EigerDescription;
    int EigerSensorThickness;
    int EigerSensorMaterial;
    int EigerXPixelSize;
    int EigerYPixelSize;
    int EigerChiStart;
    int EigerChiIncr;
    int EigerKappaStart;
    int EigerKappaIncr;
    int EigerOmega;
    int EigerOmegaStart;
    int EigerOmegaIncr;
    int EigerPhiStart;
    int EigerPhiIncr;
    int EigerTwoThetaStart;
    int EigerTwoThetaIncr;
    int EigerFlatfield;
    int EigerPhotonEnergy;
    int EigerThreshold;
    int EigerTrigger;
    int EigerTriggerExp;
    int EigerNTriggers;
    int EigerManualTrigger;
    int EigerCompressionAlgo;
    int EigerROIMode;
    int EigerPixMaskApplied;
    int EigerSWVersion;
    int EigerSerialNumber;
    int EigerState;
    int EigerError;
    int EigerThTemp0;
    int EigerThHumid0;
    int EigerLink0;
    int EigerLink1;
    int EigerLink2;
    int EigerLink3;
    int EigerDCUBufFree;
    int EigerArmed;
    int EigerSaveFiles;
    int EigerSequenceId;
    int EigerPendingFiles;
    int EigerMonitorEnable;
    int EigerMonitorTimeout;
    int EigerStreamEnable;
    int EigerStreamDropped;
    #define LAST_EIGER_PARAM EigerStreamDropped

private:
    char mHostname[512];
    RestAPI mApi;
    epicsEvent mStartEvent, mStopEvent, mTriggerEvent, mStreamEvent, mStreamDoneEvent,
            mPollDoneEvent;
    epicsMessageQueue mPollQueue, mDownloadQueue, mParseQueue, mSaveQueue,
            mReapQueue;
    bool mPollStop, mPollComplete, mStreamComplete;
    unsigned int mFrameNumber;

    // Read all parameters from detector and set some default values
    asynStatus initParams (void);

    // Wrappers to get detector parameters into asyn parameter
    asynStatus getStringP   (sys_t sys, const char *param, int dest);
    asynStatus getIntP      (sys_t sys, const char *param, int dest);
    asynStatus getDoubleP   (sys_t sys, const char *param, int dest);
    asynStatus getBinStateP (sys_t sys, const char *param, const char *oneState, int dest);
    asynStatus getBoolP     (sys_t sys, const char *param, int dest);

    // Wrappers to set parameters and catch related parameters updates
    asynStatus putString  (sys_t sys, const char *param, const char *value);
    asynStatus putInt     (sys_t sys, const char *param, int value);
    asynStatus putDouble  (sys_t sys, const char *param, double value, double epsilon = 0.0);
    asynStatus putBool    (sys_t sys, const char *param, bool value);
    void updateParams     (paramList_t *paramList);

    // File parsers
    asynStatus parseH5File   (char *buf, size_t len);
    asynStatus parseTiffFile (char *buf, size_t len);

    // Read some detector status parameters
    asynStatus eigerStatus (void);

    // Helper that returns ADStatus == ADStatusAcquire
    bool acquiring (void);
};

#define NUM_EIGER_PARAMS ((int)(&LAST_EIGER_PARAM - &FIRST_EIGER_PARAM + 1))

#endif
