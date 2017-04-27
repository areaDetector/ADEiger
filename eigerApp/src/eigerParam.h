#ifndef EIGER_PARAM_H
#define EIGER_PARAM_H

#include <string>
#include <vector>
#include <map>
#include <asynPortDriver.h>
#include <frozen.h>

#include "restApi.h"

typedef enum
{
    EIGER_P_UNINIT,
    EIGER_P_BOOL,
    EIGER_P_INT,
    EIGER_P_UINT,
    EIGER_P_DOUBLE,
    EIGER_P_STRING,
    EIGER_P_ENUM,
    EIGER_P_COMMAND,
}eiger_param_type_t;

typedef enum
{
    EIGER_ACC_RO,
    EIGER_ACC_RW,
    EIGER_ACC_WO
}eiger_access_mode_t;

typedef struct
{
    bool exists;
    union
    {
        int valInt;
        double valDouble;
    };
}eiger_min_max_t;

class EigerParamSet;

class EigerParam
{

private:
    EigerParamSet *mSet;
    std::string mAsynName;
    asynParamType mAsynType;
    sys_t mSubSystem;
    std::string mName;
    bool mRemote;

    int mAsynIndex;
    eiger_param_type_t mType;
    eiger_access_mode_t mAccessMode;
    eiger_min_max_t mMin, mMax;
    std::vector <std::string> mEnumValues, mCriticalValues;
    double mEpsilon;
    bool mCustomEnum;

    std::vector<std::string> parseArray (struct json_token *tokens,
            std::string const & name = "");
    int parseType (struct json_token *tokens, eiger_param_type_t & type);
    int parseAccessMode (struct json_token *tokens,
            eiger_access_mode_t & accessMode);
    int parseMinMax (struct json_token *tokens, std::string const & key,
            eiger_min_max_t & minMax);

    int parseValue (struct json_token *tokens, std::string & rawValue);
    int parseValue (std::string const & rawValue, bool & value);
    int parseValue (std::string const & rawValue, int & value);
    int parseValue (std::string const & rawValue, double & value);

    std::string toString (bool value);
    std::string toString (int value);
    std::string toString (double value);
    std::string toString (std::string const & value);

    int getEnumIndex (std::string const & value, size_t & index);
    bool isCritical (std::string const & value);

    int getParam (int & value);
    int getParam (double & value);
    int getParam (std::string & value);

    int setParam (int value);
    int setParam (double value);
    int setParam (std::string const & value);

    int baseFetch (std::string & rawValue, int timeout = DEFAULT_TIMEOUT);
    int basePut (std::string const & rawValue, int timeout = DEFAULT_TIMEOUT);

public:
    EigerParam (EigerParamSet *set, std::string const & asynName,
            asynParamType asynType, sys_t ss = (sys_t) 0,
            std::string const & name = "");

    void setEpsilon (double epsilon);
    int getIndex (void);
    void setEnumValues (std::vector<std::string> const & values);

    // Get the underlying asyn parameter value
    int get (bool & value);
    int get (int & value);
    int get (double & value);
    int get (std::string & value);

    // Fetch the current value from the detector, update underlying asyn parameter
    // and return the value
    int fetch (void);
    int fetch (bool & value,        int timeout = DEFAULT_TIMEOUT);
    int fetch (int & value,         int timeout = DEFAULT_TIMEOUT);
    int fetch (double & value,      int timeout = DEFAULT_TIMEOUT);
    int fetch (std::string & value, int timeout = DEFAULT_TIMEOUT);

    // Put the value both to the detector (if it is connected to a detector
    // parameter) and to the underlying asyn parameter if successful. Update
    // other modified parameters automatically.
    int put (bool value,                int timeout = DEFAULT_TIMEOUT);
    int put (int value,                 int timeout = DEFAULT_TIMEOUT);
    int put (double value,              int timeout = DEFAULT_TIMEOUT);
    int put (std::string const & value, int timeout = DEFAULT_TIMEOUT);
    int put (const char *value,         int timeout = DEFAULT_TIMEOUT);
};

typedef std::map<std::string, EigerParam*> eiger_param_map_t;
typedef std::map<int, EigerParam*> eiger_asyn_map_t;

class EigerParamSet
{
private:
    asynPortDriver *mPortDriver;
    RestAPI *mApi;
    asynUser *mUser;

    eiger_param_map_t mDetConfigMap;
    eiger_asyn_map_t mAsynMap;

public:
    EigerParamSet (asynPortDriver *portDriver, RestAPI *api, asynUser *user);

    EigerParam *create(std::string const & asynName, asynParamType asynType,
            sys_t ss = (sys_t)0, std::string const & name = "");

    asynPortDriver *getPortDriver (void);
    RestAPI *getApi (void);
    EigerParam *getByName (std::string const & name);
    EigerParam *getByIndex (int index);
    asynUser *getUser (void);
    int fetchAll (void);

    int fetchParams (std::vector<std::string> const & params);
};



#endif
