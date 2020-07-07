#include <stdlib.h>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <limits>

#include <frozen.h>
#include <ADDriver.h>
#include <math.h>
#include "eigerParam.h"

// Error message formatters
#define ERR(msg) asynPrint(mSet->getUser(), ASYN_TRACE_ERROR,\
    "Param[%s]::%s: %s\n", \
    mAsynName.c_str(), functionName, msg)

#define ERR_ARGS(fmt,...) asynPrint(mSet->getUser(), ASYN_TRACE_ERROR, \
    "Param[%s]::%s: " fmt "\n", mAsynName.c_str(), functionName, __VA_ARGS__);

// Flow message formatters
#define FLOW(msg) asynPrint(mSet->getUser(), ASYN_TRACE_FLOW, \
    "Param[%s]::%s: %s\n", \
    mAsynName.c_str(), functionName, msg)

#define FLOW_ARGS(fmt,...) asynPrint(mSet->getUser(), ASYN_TRACE_FLOW, \
    "Param[%s]::%s: " fmt "\n", mAsynName.c_str(), functionName, __VA_ARGS__);


#define MAX_BUFFER_SIZE 128
#define MAX_MESSAGE_SIZE 512
#define MAX_JSON_TOKENS 100

using std::string;
using std::vector;
using std::map;
using std::pair;

vector<string> EigerParam::parseArray (struct json_token *tokens,
        string const & name)
{
    vector<string> arrayValues;
    struct json_token *t;
    if(name.empty())
        t = tokens;
    else
        t = find_json_token(tokens, name.c_str());

    if(t)
    {
        if(t->type == JSON_TYPE_ARRAY)
        {
            arrayValues.reserve(t->num_desc);
            for(int i = 1; i <= t->num_desc; ++i)
                arrayValues.push_back(string((t+i)->ptr, (t+i)->len));
        }
        else if(t->type == JSON_TYPE_STRING)
            arrayValues.push_back(string(t->ptr, t->len));
    }
    return arrayValues;
}

int EigerParam::parseType (struct json_token *tokens, eiger_param_type_t & type)
{
    const char *functionName = "parseType";

    // Find value type
    struct json_token *token = find_json_token(tokens, "value_type");
    if(token == NULL)
    {
        ERR("unable to find 'value_type' json field");
        return EXIT_FAILURE;
    }
    string typeStr(token->ptr, token->len);

    // Check if this parameter is an enumeration
    token = find_json_token(tokens, "allowed_values");
    if(token)
        typeStr = "enum";

    // Check if this parameter is write only (command)
    token = find_json_token(tokens, "access_mode");
    if(token && token->ptr[0] == 'w')
        typeStr = "command";

    // Map type to asynParamType
    switch(typeStr[0])
    {
    case 's': type = EIGER_P_STRING;  break;
    // "string" changed to "list" in 1.8.0 as of EIGER2 v2020.1
    case 'l': type = EIGER_P_STRING;  break;
    case 'f': type = EIGER_P_DOUBLE;  break;
    case 'b': type = EIGER_P_BOOL;    break;
    case 'u': type = EIGER_P_UINT;    break;
    case 'i': type = EIGER_P_INT;     break;
    case 'e': type = EIGER_P_ENUM;    break;
    case 'c': type = EIGER_P_COMMAND; break;
    default:
        ERR_ARGS("unrecognized value type '%s'", typeStr.c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int EigerParam::parseAccessMode (struct json_token *tokens,
        eiger_access_mode_t & accessMode)
{
    const char *functionName = "parseAccessMode";

    struct json_token *t = find_json_token(tokens, "access_mode");
    if(!t)
        return EXIT_FAILURE;

    string accessModeStr(t->ptr, t->len);
    if(accessModeStr == "r")
        accessMode = EIGER_ACC_RO;
    else if(accessModeStr == "w")
        accessMode = EIGER_ACC_WO;
    else if(accessModeStr == "rw")
        accessMode = EIGER_ACC_RW;
    else
    {
        ERR_ARGS("invalid access mode '%s", accessModeStr.c_str());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int EigerParam::parseMinMax (struct json_token *tokens, string const & key,
        eiger_min_max_t & minMax)
{
    const char *functionName = "parseMinMax";

    struct json_token *t = find_json_token(tokens, key.c_str());

    if((minMax.exists = (t != NULL)))
    {
        struct json_token *type = find_json_token(tokens, "value_type");
        if(!type)
        {
            ERR("failed to find 'value_type'");
            return EXIT_FAILURE;
        }

        string value(t->ptr, t->len);
        if(type->ptr[0] == 'i' || type->ptr[0] == 'u')
        {
            if(sscanf(value.c_str(), "%d", &minMax.valInt) != 1)
            {
                ERR_ARGS("failed to parse '%s' as integer", value.c_str());
                return EXIT_FAILURE;
            }
        }
        else if(type->ptr[0] == 'f')
        {
            if(sscanf(value.c_str(), "%lf", &minMax.valDouble) != 1)
            {
                ERR_ARGS("failed to parse '%s' as double", value.c_str());
                return EXIT_FAILURE;
            }
        }
    }

    return EXIT_SUCCESS;
}

int EigerParam::parseValue (struct json_token *tokens, std::string & rawValue)
{
    const char *functionName = "parseValue";

    struct json_token *token = find_json_token(tokens, "value");
    if(token == NULL)
    {
        ERR("unable to find 'value' json field");
        return EXIT_FAILURE;
    }
    rawValue = string(token->ptr, token->len);
    return EXIT_SUCCESS;
}

int EigerParam::parseValue (std::string const & rawValue, bool & value)
{
    const char *functionName = "parseValue";

    if(rawValue == "true")
        value = true;
    else if(rawValue == "false")
        value = false;
    else
    {
        ERR_ARGS("couldn't parse value '%s' as boolean", rawValue.c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int EigerParam::parseValue (std::string const & rawValue, int & value)
{
    const char *functionName = "parseValue";

    if(sscanf(rawValue.c_str(), "%d", &value) != 1)
    {
        ERR_ARGS("couldn't parse value '%s' as integer", rawValue.c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int EigerParam::parseValue (std::string const & rawValue, double & value)
{
    const char *functionName = "parseValue";

    if(sscanf(rawValue.c_str(), "%lf", &value) != 1)
    {
        ERR_ARGS("couldn't parse value '%s' as double", rawValue.c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

std::string EigerParam::toString (bool value)
{
    if(mType == EIGER_P_ENUM)
        return toString(mEnumValues[(int) (!value)]);

    return value ? "true" : "false";
}

std::string EigerParam::toString (int value)
{
    if(mType == EIGER_P_ENUM)
        return toString(mEnumValues[value]);

    std::ostringstream os;
    os << value;
    return os.str();
}

std::string EigerParam::toString (double value)
{
    std::ostringstream os;
    os << std::setprecision(std::numeric_limits<long double>::digits10 + 2) << value;
    return os.str();
}

std::string EigerParam::toString (std::string const & value)
{
    std::ostringstream os;
    os << "\"" << value << "\"";
    return os.str();
}

int EigerParam::getEnumIndex (std::string const & value, size_t & index)
{
    const char *functionName = "getEnumIndex";
    for(index = 0; index < mEnumValues.size(); ++index)
        if(mEnumValues[index] == value)
            return EXIT_SUCCESS;

    ERR_ARGS("[param=%s] can't find index of value %s", mAsynName.c_str(),
            value.c_str());
    return EXIT_FAILURE;
}

bool EigerParam::isCritical (std::string const & value)
{
    size_t i;
    for(i = 0; i < mCriticalValues.size(); ++i)
        if(mCriticalValues[i] == value)
            return true;
    return false;
}

int EigerParam::getParam (int & value)
{
    return (int) mSet->getPortDriver()->getIntegerParam(mAsynIndex, &value);
}

int EigerParam::getParam (double & value)
{
    return (int) mSet->getPortDriver()->getDoubleParam(mAsynIndex, &value);
}

int EigerParam::getParam (std::string & value)
{
    return (int) mSet->getPortDriver()->getStringParam(mAsynIndex, value);
}

int EigerParam::setParam (int value)
{
    return (int) mSet->getPortDriver()->setIntegerParam(mAsynIndex, value);
}

int EigerParam::setParam (double value)
{
    return (int) mSet->getPortDriver()->setDoubleParam(mAsynIndex, value);
}

int EigerParam::setParam (std::string const & value)
{
    return (int) mSet->getPortDriver()->setStringParam(mAsynIndex, value);
}


EigerParam::EigerParam (EigerParamSet *set, string const & asynName,
        asynParamType asynType, sys_t ss, string const & name)
: mSet(set), mAsynName(asynName), mAsynType(asynType), mSubSystem(ss),
  mName(name), mRemote(!mName.empty()), mAsynIndex(-1),
  mType(EIGER_P_UNINIT), mAccessMode(), mMin(), mMax(), mEnumValues(),
  mCriticalValues(), mEpsilon(0.0), mCustomEnum(false)
{
    const char *functionName = "EigerParam";

    asynStatus status;

    // Check if asyn parameter already exists, create if it doesn't
    if(mSet->getPortDriver()->findParam(mAsynName.c_str(), &mAsynIndex))
    {
        status = mSet->getPortDriver()->createParam(mAsynName.c_str(), mAsynType, &mAsynIndex);
        if(status)
        {
            ERR_ARGS("[param=%s] failed to create param", mAsynName.c_str());
            throw std::runtime_error(mAsynName);
        }
    }

    if(mName.empty())
    {
        switch(asynType)
        {
        case asynParamInt32:   mType = EIGER_P_INT;    break;
        case asynParamFloat64: mType = EIGER_P_DOUBLE; break;
        case asynParamOctet:   mType = EIGER_P_STRING; break;
        default:
            ERR_ARGS("[param=%s] invalid asyn type %d", mAsynName.c_str(),
                    (int)asynType);
            throw std::runtime_error(mAsynName);
        }
    }
    else if(ss == SSCommand || ss == SSFWCommand || ss == SSSysCommand)
    {
        mType = EIGER_P_COMMAND;
        mAccessMode = EIGER_ACC_WO;
    }
}

void EigerParam::setEpsilon (double epsilon)
{
    mEpsilon = epsilon;
}

int EigerParam::getIndex (void)
{
    return mAsynIndex;
}

void EigerParam::setEnumValues (vector<string> const & values)
{
    mEnumValues = values;
    mCustomEnum = true;
}

int EigerParam::get (bool & value)
{
    if(mAsynType == asynParamInt32)
    {
        int tempValue;
        getParam(tempValue);
        value = (bool) tempValue;
    }
    else if(mAsynType == asynParamOctet && mEnumValues.size() == 2)
    {
        int tempValue;
        if(get(tempValue))
            return EXIT_FAILURE;
        value = (bool) tempValue;
    }
    else
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

int EigerParam::get (int & value)
{
    if(mAsynType == asynParamInt32)
        getParam(value);
    else if(mAsynType == asynParamOctet && mEnumValues.size())
    {
        string tempValue;
        getParam(tempValue);

        size_t index;
        if(getEnumIndex(tempValue, index))
            return EXIT_FAILURE;

        value = (int) index;
    }
    else
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

int EigerParam::get (double & value)
{
    return (int) getParam(value);
}

int EigerParam::get (std::string & value)
{
    if(mType == EIGER_P_ENUM && mAsynType == asynParamInt32)
    {
        int index;
        int status = getParam(index);
        value = mEnumValues[index];
        return status;
    }

    return (int) getParam(value);
}

int EigerParam::baseFetch (string & rawValue, int timeout)
{
    const char *functionName = "baseFetch";
    if(!mRemote)
    {
        ERR_ARGS("[param=%s] can't fetch local parameter", mAsynName.c_str());
        return EXIT_FAILURE;
    }

    if(mAccessMode == EIGER_ACC_WO)
        return EXIT_SUCCESS;

    string buffer;
    mSet->getApi()->get(mSubSystem, mName, buffer, timeout);

    // Parse JSON
    struct json_token tokens[MAX_JSON_TOKENS];
    int err = parse_json(buffer.c_str(), buffer.size(), tokens, MAX_JSON_TOKENS);
    if(err < 0)
    {
        const char *msg = "unable to parse json response";
        ERR_ARGS("[param=%s] %s\n[%s]", mName.c_str(), msg, buffer.c_str());
        return EXIT_FAILURE;
    }

    if(mType == EIGER_P_UNINIT)
    {
        if(parseType(tokens, mType))
        {
            const char *msg = "unable to parse parameter type";
            ERR_ARGS("[param=%s] %s\n[%s]", mName.c_str(), msg, buffer.c_str());
            return EXIT_FAILURE;
        }

        switch(mSubSystem)
        {
        case SSCommand:
        case SSFWCommand:
        case SSSysCommand:
            mAccessMode = EIGER_ACC_WO;
            break;
        case SSDetStatus:
        case SSFWStatus:
        case SSMonStatus:
        case SSStreamStatus:
            mAccessMode = EIGER_ACC_RO;
            break;
        default:
            if(parseAccessMode(tokens, mAccessMode))
                mAccessMode = EIGER_ACC_RO;
        }

        if(mCustomEnum)
            mType = EIGER_P_ENUM;
        else
            mEnumValues = parseArray(tokens, "allowed_values");
        mCriticalValues = parseArray(tokens, "critical_values");
    }

    if(mType == EIGER_P_INT || mType == EIGER_P_UINT || mType == EIGER_P_DOUBLE)
    {
        if(parseMinMax(tokens, "min", mMin))
        {
            const char *msg = "unable to parse min limit";
            ERR_ARGS("[param=%s] %s\n[%s]", mName.c_str(), msg, buffer.c_str());
            return EXIT_FAILURE;
        }

        if(parseMinMax(tokens, "max", mMax))
        {
            const char *msg = "unable to parse max limit";
            ERR_ARGS("[param=%s] %s\n[%s]", mName.c_str(), msg, buffer.c_str());
            return EXIT_FAILURE;
        }
    }
    else if(mType == EIGER_P_ENUM)
    {
        mMin.exists = true;
        mMax.exists = true;
        mMin.valInt = 0;
        mMax.valInt = (int) (mEnumValues.size() - 1);
    }

    if(parseValue(tokens, rawValue))
    {
        const char *msg = "unable to parse raw value";
        ERR_ARGS("[param=%s] %s\n[%s]", mName.c_str(), msg, buffer.c_str());
        return EXIT_FAILURE;
    }

    FLOW_ARGS("%s", rawValue.c_str());
    return EXIT_SUCCESS;
}

int EigerParam::fetch (bool & value, int timeout)
{
    const char *functionName = "fetch<bool>";
    if(mRemote && mType != EIGER_P_COMMAND)
    {
        string rawValue;
        if(baseFetch(rawValue))
        {
            ERR_ARGS("[param=%s] underlying baseFetch failed",
                    mAsynName.c_str());
            return EXIT_FAILURE;

        }
        if(mType == EIGER_P_BOOL)
        {
            if(parseValue(rawValue, value))
                return EXIT_FAILURE;
        }
        else if(mType == EIGER_P_ENUM)
        {
            if(mEnumValues.size() != 2)
            {
                ERR_ARGS("[param=%s] can't fetch non-binary enum as bool\n",
                        mAsynName.c_str());
                return EXIT_FAILURE;
            }

            size_t index;
            if(getEnumIndex(rawValue, index))
                return EXIT_FAILURE;

            value = (bool) index;
            if(setParam((int) index))
            {
                ERR_ARGS("[param=%s] failed to set asyn parameter",
                        mAsynName.c_str());
                return EXIT_FAILURE;
            }
        }
        else
        {
            ERR_ARGS("[param=%s] unexpected type %d", mAsynName.c_str(),
                    mType);
            return EXIT_FAILURE;
        }
    }

    FLOW_ARGS("%d", value);
    return get(value);
}

int EigerParam::fetch (int & value, int timeout)
{
    const char *functionName = "fetch<int>";
    if(mRemote && mType != EIGER_P_COMMAND)
    {
        string rawValue;
        if(baseFetch(rawValue))
        {
            ERR_ARGS("[param=%s] underlying baseFetch failed",
                    mAsynName.c_str());
            return EXIT_FAILURE;

        }

        if(mType == EIGER_P_ENUM)
        {
            size_t index;
            if(getEnumIndex(rawValue, index))
                return EXIT_FAILURE;

            value = (int) index;
        }
        else if(mType == EIGER_P_BOOL)
        {
            bool tempValue;
            if(parseValue(rawValue, tempValue))
                return EXIT_FAILURE;
            value = (int) tempValue;
        }
        else if(mType == EIGER_P_INT || mType == EIGER_P_UINT)
        {
            if(parseValue(rawValue, value))
                return EXIT_FAILURE;
        }
        else
        {
            ERR_ARGS("[param=%s] unexpected type %d", mAsynName.c_str(),
                    mType);
            return EXIT_FAILURE;
        }

        if(setParam(value))
        {
            ERR_ARGS("[param=%s] failed to set asyn parameter",
                    mAsynName.c_str());
            return EXIT_FAILURE;
        }
    }

    FLOW_ARGS("%d", value);
    return get(value);
}

int EigerParam::fetch (double & value, int timeout)
{
    const char *functionName = "fetch<double>";
    if(mRemote && mType != EIGER_P_COMMAND)
    {
        if(mType != EIGER_P_DOUBLE && mType != EIGER_P_UNINIT)
        {
            ERR_ARGS("[param=%s] unexpected type %d", mAsynName.c_str(),
                    mType);
            return EXIT_FAILURE;
        }

        string rawValue;
        if(baseFetch(rawValue))
        {
            ERR_ARGS("[param=%s] underlying baseFetch failed",
                    mAsynName.c_str());
            return EXIT_FAILURE;
        }

        if(parseValue(rawValue, value))
            return EXIT_FAILURE;

        if(setParam(value))
        {
            ERR_ARGS("[param=%s] failed to set asyn parameter",
                    mAsynName.c_str());
            return EXIT_FAILURE;
        }
    }

    FLOW_ARGS("%lf", value);
    return get(value);
}

int EigerParam::fetch (std::string & value, int timeout)
{
    const char *functionName = "fetch<string>";

    if(mRemote && mType != EIGER_P_COMMAND)
    {
        if(mType != EIGER_P_STRING && mType != EIGER_P_ENUM &&
           mType != EIGER_P_UNINIT)
            return EXIT_FAILURE;

        if(baseFetch(value))
        {
            ERR_ARGS("[param=%s] underlying baseFetch failed",
                    mAsynName.c_str());
            return EXIT_FAILURE;

        }

        // TODO: check if it is critical

        if(setParam(value))
        {
            ERR_ARGS("[param=%s] failed to set asyn parameter",
                    mAsynName.c_str());
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    FLOW_ARGS("%s", value.c_str());
    return get(value);
}

int EigerParam::fetch (void)
{
    switch(mAsynType)
    {
    case asynParamInt32:
    {
        int dummy;
        return fetch(dummy);
    }
    case asynParamFloat64:
    {
        double dummy;
        return fetch(dummy);
    }
    case asynParamOctet:
    {
        string dummy;
        return fetch(dummy);
    }

    default:
        break;
    }
    return EXIT_SUCCESS;
}

int EigerParam::basePut (const std::string & rawValue, int timeout)
{
    const char *functionName = "basePut";
    FLOW_ARGS("'%s'", rawValue.c_str());
    if(mAccessMode == EIGER_ACC_RO)
    {
        ERR_ARGS("[param=%s] can't write to read-only parameter", mAsynName.c_str());
        return EXIT_FAILURE;
    }

    string reply;
    if(mSet->getApi()->put(mSubSystem, mName, rawValue, &reply, timeout))
    {
        ERR_ARGS("[param=%s] underlying RestAPI put failed", mAsynName.c_str());
        return EXIT_FAILURE;
    }

    // Parse JSON
    if(!reply.empty())
    {
        struct json_token tokens[MAX_JSON_TOKENS];
        int err = parse_json(reply.c_str(), reply.size(), tokens, MAX_JSON_TOKENS);
        if(err < 0)
        {
            const char *msg = "unable to parse json response";
            ERR_ARGS("[param=%s] %s\n[%s]", mName.c_str(), msg, reply.c_str());
            return EXIT_FAILURE;
        }

        mSet->fetchParams(parseArray(tokens));
    }
    return EXIT_SUCCESS;
}

int EigerParam::put (bool value, int timeout)
{
    const char *functionName = "put<bool>";
    FLOW_ARGS("%d", value);
    if(!mRemote)
    {
        setParam((int)value);
        return EXIT_SUCCESS;
    }

    if(mType == EIGER_P_UNINIT && fetch())
        return EXIT_FAILURE;

    if(mType != EIGER_P_BOOL && mType != EIGER_P_ENUM)
        return EXIT_FAILURE;

    int status;
    if(mType == EIGER_P_ENUM)
    {
        if(mEnumValues.size() != 2)
            return EXIT_FAILURE;

        // XOR with mReversedEnum
        status = basePut(toString(value), timeout);
    }
    else
        status = basePut(toString(value), timeout);

    if(status)
        return EXIT_FAILURE;

    if(setParam(value))
    {
        ERR_ARGS("[param=%s] failed to set asyn parameter",
                mAsynName.c_str());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int EigerParam::put (int value, int timeout)
{
    const char *functionName = "put<int>";
    int status;
    FLOW_ARGS("%d", value);
    if(mRemote)
    {
        if(mType == EIGER_P_UNINIT && fetch())
            return EXIT_FAILURE;

        if(mType != EIGER_P_BOOL && mType != EIGER_P_INT &&
           mType != EIGER_P_UINT && mType != EIGER_P_ENUM &&
           mType != EIGER_P_COMMAND)
        {
            ERR_ARGS("[param=%s] expected bool, int, uint or enum",
                    mAsynName.c_str());
            return EXIT_FAILURE;
        }

        // Clamp value
        if(mMin.exists && value < mMin.valInt)
            value = mMin.valInt;

        if(mMax.exists && value > mMax.valInt)
            value = mMax.valInt;

        // Protect against trying to write negative values to an unsigned int
        if(mType == EIGER_P_UINT && value < 0)
            value = 0;

        if(mType == EIGER_P_BOOL)
            status = basePut(toString((bool)value), timeout);
        else
            status = basePut(toString(value), timeout);

        if(status)
        {
            ERR_ARGS("[param=%s] underlying basePut failed",
                    mAsynName.c_str());
            return EXIT_FAILURE;
        }
    }

    if(mAsynType == asynParamInt32)
        status = setParam(value);
    else
        status = setParam(mEnumValues[value]);

    if(status)
    {
        ERR_ARGS("[param=%s] failed to set asyn parameter",
                mAsynName.c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int EigerParam::put (double value, int timeout)
{
    const char *functionName = "put<double>";
    FLOW_ARGS("%lf", value);
    if(mEpsilon)
    {
        double currentValue;
        getParam(currentValue);
        if(fabs(currentValue - value) < mEpsilon)
            return EXIT_SUCCESS;
    }

    if(mRemote)
    {
        if(mType == EIGER_P_UNINIT && fetch())
            return EXIT_FAILURE;

        if(mType != EIGER_P_DOUBLE)
            return EXIT_FAILURE;

        // Clamp value
        if(mMin.exists && (value < mMin.valDouble))
        {
            value = mMin.valDouble;
            ERR_ARGS("clamped to min %lf", value);
        }

        if(mMax.exists && (value > mMax.valDouble))
        {
            value = mMax.valDouble;
            ERR_ARGS("clamped to max %lf", value);
        }

        if(basePut(toString(value), timeout))
            return EXIT_FAILURE;
    }

    if(setParam(value))
    {
        ERR_ARGS("[param=%s] failed to set asyn parameter",
                mAsynName.c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int EigerParam::put (std::string const & value, int timeout)
{
    const char *functionName = "put<string>";
    FLOW_ARGS("%s", value.c_str());
    if(!mRemote)
    {
        setParam(value);
        return EXIT_SUCCESS;
    }

    if(mType == EIGER_P_UNINIT && fetch())
        return EXIT_FAILURE;

    if(mType != EIGER_P_STRING && mType != EIGER_P_ENUM)
        return EXIT_FAILURE;

    size_t index;
    if(mType == EIGER_P_ENUM && getEnumIndex(value, index))
        return EXIT_FAILURE;

    if(basePut(toString(value), timeout))
        return EXIT_FAILURE;

    int status;
    if(mAsynType == asynParamInt32)
        status = setParam((int)index);
    else
        status = setParam(value);

    if(status)
    {
        ERR_ARGS("[param=%s] failed to set asyn parameter",
                mAsynName.c_str());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int EigerParam::put (const char * value, int timeout)
{
    return put(string(value), timeout);
}

EigerParamSet::EigerParamSet (asynPortDriver *portDriver, RestAPI *api,
        asynUser *user)
: mPortDriver(portDriver), mApi(api), mUser(user), mDetConfigMap(), mAsynMap()
{}

EigerParam *EigerParamSet::create(string const & asynName,
        asynParamType asynType, sys_t ss, string const & name)
{
    EigerParam *p = new EigerParam(this, asynName, asynType, ss, name);
    if(!name.empty() && ss == SSDetConfig)
        mDetConfigMap.insert(std::make_pair(name, p));

    mAsynMap.insert(std::make_pair(p->getIndex(), p));
    return p;
}

asynPortDriver *EigerParamSet::getPortDriver (void)
{
    return mPortDriver;
}

RestAPI *EigerParamSet::getApi (void)
{
    return mApi;
}

EigerParam *EigerParamSet::getByName (string const & name)
{
    eiger_param_map_t::iterator item(mDetConfigMap.find(name));

    if(item != mDetConfigMap.end())
        return item->second;
    return NULL;
}

EigerParam *EigerParamSet::getByIndex (int index)
{
    eiger_asyn_map_t::iterator item(mAsynMap.find(index));

    if(item != mAsynMap.end())
        return item->second;
    return NULL;
}

asynUser *EigerParamSet::getUser (void)
{
    return mUser;
}

int EigerParamSet::fetchAll (void)
{
    int status = EXIT_SUCCESS;

    eiger_asyn_map_t::iterator it;
    for(it = mAsynMap.begin(); it != mAsynMap.end(); ++it)
        status |= it->second->fetch();

    return status;
}

int EigerParamSet::fetchParams (vector<string> const & params)
{
    int status = EXIT_SUCCESS;
    vector<string>::const_iterator param;

    for(param = params.begin(); param != params.end(); ++param)
    {
        EigerParam *p = getByName(*param);
        if(p)
            status |= p->fetch();
    }

    return status;
}
