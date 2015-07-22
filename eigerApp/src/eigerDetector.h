#ifndef EIGER_DETECTOR_H
#define EIGER_DETECTOR_H

#include "eigerApi.h"

// FileWriter Parameters
#define EigerFWClearString              "CLEAR"
#define EigerFWCompressionString        "COMPRESSION"
#define EigerFWNImgsPerFileString       "NIMAGES_PER_FILE"

// Acquisition Metadata Parameters
#define EigerBeamXString                "BEAM_X"
#define EigerBeamYString                "BEAM_Y"
#define EigerDetDistString              "DET_DIST"
#define EigerWavelengthString           "WAVELENGTH"

// Acquisition Parameters
#define EigerFlatfieldString            "FLATFIELD_APPLIED"
#define EigerPhotonEnergyString         "PHOTON_ENERGY"
#define EigerThresholdString            "THRESHOLD"
#define EigerTriggerExpString           "TRIGGER_EXPOSURE"
#define EigerNTriggersString            "NUM_TRIGGERS"

// Detector Info Parameters
#define EigerSWVersionString            "SW_VERSION"

// Detector Status Parameters
#define EigerThTemp0String              "TH_TEMP_0"
#define EigerThHumid0String             "TH_HUMID_0"
#define EigerLink0String                "LINK_0"
#define EigerLink1String                "LINK_1"
#define EigerLink2String                "LINK_2"
#define EigerLink3String                "LINK_3"

// Commands
#define EigerArmString                  "ARM"
#define EigerTriggerString              "TRIGGER"
#define EigerDisarmString               "DISARM"
#define EigerCancelString               "CANCEL"

// Other Parameters
#define EigerArmedString                "ARMED"
#define EigerSaveFilesString            "SAVE_FILES"
#define EigerSequenceIdString           "SEQ_ID"
#define EigerPendingFilesString         "PENDING_FILES"

//  Driver for the Dectris' Eiger pixel array detector using their REST server
class eigerDetector : public ADDriver
{
public:
    eigerDetector(const char *portName, const char *serverHostname,
            int maxBuffers, size_t maxMemory, int priority, int stackSize);

    // These are the methods that we override from ADDriver
    virtual asynStatus writeInt32  (asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    void report(FILE *fp, int details);

    // These should be private but are called from C so must be public
    void controlTask  (void);
    void pollTask     (void);
    void downloadTask (void);
    void streamTask   (void);
    void saveTask     (void);
    void reapTask     (void);

protected:
    int EigerFWClear;
    #define FIRST_EIGER_PARAM EigerFWClear
    int EigerFWCompression;
    int EigerFWNImgsPerFile;
    int EigerBeamX;
    int EigerBeamY;
    int EigerDetDist;
    int EigerWavelength;
    int EigerFlatfield;
    int EigerPhotonEnergy;
    int EigerThreshold;
    int EigerTriggerExp;
    int EigerNTriggers;
    int EigerSWVersion;
    int EigerThTemp0;
    int EigerThHumid0;
    int EigerLink0;
    int EigerLink1;
    int EigerLink2;
    int EigerLink3;
    int EigerArm;
    int EigerTrigger;
    int EigerDisarm;
    int EigerCancel;
    int EigerArmed;
    int EigerSaveFiles;
    int EigerSequenceId;
    int EigerPendingFiles;
    #define LAST_EIGER_PARAM EigerPendingFiles

private:
    char mHostname[512];
    Eiger eiger;
    epicsMessageQueue mCommandQueue, mPollQueue, mDownloadQueue, mStreamQueue,
        mSaveQueue, mReapQueue;

    // Wrappers to get detector parameters into asyn parameter
    asynStatus getStringP (sys_t sys, const char *param, int dest);
    asynStatus getIntP    (sys_t sys, const char *param, int dest);
    asynStatus getDoubleP (sys_t sys, const char *param, int dest);
    asynStatus getBoolP   (sys_t sys, const char *param, int dest);

    // Nice wrappers to set parameters and catch related parameters updates
    asynStatus putString  (sys_t sys, const char *param, const char *value);
    asynStatus putInt     (sys_t sys, const char *param, int value);
    asynStatus putDouble  (sys_t sys, const char *param, double value);
    asynStatus putBool    (sys_t sys, const char *param, bool value);
    void updateParams     (paramList_t *paramList);

    // HDF5 parser
    asynStatus parseH5File (char *buf, size_t len);

    // Read some detector status parameters
    asynStatus eigerStatus (void);
};

#define NUM_EIGER_PARAMS ((int)(&LAST_EIGER_PARAM - &FIRST_EIGER_PARAM + 1))

#endif
