#ifndef EIGER_DETECTOR_H
#define EIGER_DETECTOR_H

#include "eigerApi.h"

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

/*
 *  Driver for the Dectris' Eiger pixel array detector using their REST server
 */
class eigerDetector : public ADDriver
{
public:
    eigerDetector(const char *portName, const char *serverHostname,
            int maxBuffers, size_t maxMemory, int priority, int stackSize);

    /* These are the methods that we override from ADDriver */
    virtual asynStatus writeInt32  (asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus writeOctet  (asynUser *pasynUser, const char *value,
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
    epicsEvent disarmEvent, startEvent, stopEvent;
    Eiger eiger;

    /*
     * Nice wrappers to get parameters
     */
    asynStatus getStringP (sys_t sys, const char *param, int dest);
    asynStatus getIntP    (sys_t sys, const char *param, int dest);
    asynStatus getDoubleP (sys_t sys, const char *param, int dest);
    asynStatus getBoolP   (sys_t sys, const char *param, int dest);

    /*
     * Nice wrappers to set parameters and catch parameters updates
     */
    asynStatus putString  (sys_t sys, const char *param, const char *value);
    asynStatus putInt     (sys_t sys, const char *param, int value);
    asynStatus putDouble  (sys_t sys, const char *param, double value);
    asynStatus putBool    (sys_t sys, const char *param, bool value);
    void updateParams (paramList_t *paramList);

    /*
     * File getters
     */
    asynStatus saveFile (const char *file, char *data, size_t len);

    /*
     * Arm, trigger and disarm
     */
    asynStatus capture (triggerMode_t triggerMode, double triggerTimeout);

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

#endif
