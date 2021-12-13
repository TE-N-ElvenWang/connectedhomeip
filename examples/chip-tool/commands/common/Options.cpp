
#include "Options.h"

#include <sstream>

#include <lib/core/CHIPError.h>
#include <lib/support/CHIPArgParser.hpp>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::ArgParser;

namespace {

ChiptoolCommandOptions gChiptoolCommandOptions;
enum
{
    kCommandOption_Name = 0x100,
    kCommandOption_NodeId = 0x101,
};

OptionDef sCommandOptionDefs[]  = { 
                                    { "name", kArgumentRequired, kCommandOption_Name },
                                    { "nodeid", kArgumentRequired, kCommandOption_NodeId },
                                    {} 
                                  };
const char * sCommandOptionHelp = "  --name <str>\n"
                                  "       The target device's alias name. Used to align with communication sessions.\n"
                                  "  --nodeid <number>\n"
                                  "       The node id of the target device.\n"
                                  "\n";

bool HandleOption(const char * aProgram, OptionSet * aOptions, int aIdentifier, const char * aName, const char * aValue)
{
    switch (aIdentifier)
    {
    case kCommandOption_Name:
        if (aValue != NULL)
        {
            gChiptoolCommandOptions.GetInstance().DeviceName = std::string(aValue);
            return true;
        }
        break;
    case kCommandOption_NodeId:
        if (aValue != NULL)
        {
            std::stringstream ss(aValue);
            ss >> gChiptoolCommandOptions.GetInstance().nodeId;
            ChipLogError(ReportFilter, "sendrolon get nodeid: 0x%lx", gChiptoolCommandOptions.GetInstance().nodeId);
            return true;
        }
        break;
    }

    return true;
}

bool HandleNonOptionArgs(const char * progName, int argc, char * argv[])
{
    return true;
}

OptionSet sCommandOptions             = { HandleOption, sCommandOptionDefs, "CHIP_TOOL_OPTIONS", sCommandOptionHelp };
OptionSet * sChiptoolCommandOptions[] = { &sCommandOptions, nullptr };

} // namespace

CHIP_ERROR ParseArguments(int argc, char * argv[])
{
    gChiptoolCommandOptions.GetInstance().nodeId = 0;
    if (!chip::ArgParser::ParseArgs(argv[0], argc, argv, sChiptoolCommandOptions, HandleNonOptionArgs, true))
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    return CHIP_NO_ERROR;
}

ChiptoolCommandOptions & ChiptoolCommandOptions::GetInstance()
{
    return gChiptoolCommandOptions;
}
