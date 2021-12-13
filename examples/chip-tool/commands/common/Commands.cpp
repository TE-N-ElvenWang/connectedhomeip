/*
 *   Copyright (c) 2020 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#include "Commands.h"

#include "Command.h"

#include "Options.h"

#include <algorithm>
#include <string>

#if CONFIG_DEVICE_LAYER
#include <platform/CHIPDeviceLayer.h>
#endif

#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ScopedBuffer.h>
#include <sys/stat.h>
#include <sys/types.h>

#define FIFO_PATH "/tmp/chip_tool_fifo"
#define FIFO_BUF_SZ 1024

void Commands::Register(const char * clusterName, commands_list commandsList)
{
    for (auto & command : commandsList)
    {
        mClusters[clusterName].push_back(std::move(command));
    }
}

int Commands::Run(int argc, char ** argv)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::Controller::CommissionerInitParams initParams;
    Command * command = nullptr;
    NodeId localId;
    NodeId remoteId;
#ifdef CONFIG_CHIP_TOOL_SERVER
    char read_buf[FIFO_BUF_SZ];
    int ret = 0;
    int fd;
    char * args[20] = { 0 };
    char ** args_pass;
    int argn                = 0;
    char * split_ptr        = NULL;
    char * delim            = NULL;
    const char * space_char = " ";
#endif

    chip::Platform::ScopedMemoryBuffer<uint8_t> noc;
    chip::Platform::ScopedMemoryBuffer<uint8_t> icac;
    chip::Platform::ScopedMemoryBuffer<uint8_t> rcac;

    err = chip::Platform::MemoryInit();
    VerifyOrExit(err == CHIP_NO_ERROR, ChipLogError(Controller, "Init Memory failure: %s", chip::ErrorStr(err)));
#if !defined(CONFIG_CHIP_TOOL_SERVER)
    if (CHIP_NO_ERROR == ParseArguments(argc, argv))
    {
        if (ChiptoolCommandOptions::GetInstance().DeviceName.length() != 0)
        {
            char * processName = argv[0];
            argv += 2;
            *argv = processName;
            argc -= 2;
        }
        if (ChiptoolCommandOptions::GetInstance().nodeId != 0)
        {
            char * processName = argv[0];
            argv += 2;
            *argv = processName;
            argc -= 2;
        }
    }
#endif

#if CHIP_DEVICE_LAYER_TARGET_LINUX && CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
    // By default, Linux device is configured as a BLE peripheral while the controller needs a BLE central.
    SuccessOrExit(err = chip::DeviceLayer::Internal::BLEMgrImpl().ConfigureBle(/* BLE adapter ID */ 0, /* BLE central */ true));
#endif

    err = mStorage.Init(ChiptoolCommandOptions::GetInstance().DeviceName.c_str());
    VerifyOrExit(err == CHIP_NO_ERROR, ChipLogError(Controller, "Init Storage failure: %s", chip::ErrorStr(err)));

    chip::Logging::SetLogFilter(mStorage.GetLoggingLevel());
    localId  = mStorage.GetLocalNodeId();
    remoteId = mStorage.GetRemoteNodeId();
    ChiptoolCommandOptions::GetInstance().nodeDict.insert({ remoteId, ChiptoolCommandOptions::GetInstance().DeviceName });
    ChipLogError(Controller, "remoteId(0x%lx) assigned to DeviceName:%s", remoteId,
                 ChiptoolCommandOptions::GetInstance().nodeDict[remoteId].c_str());

    ChipLogProgress(Controller, "Read local id 0x" ChipLogFormatX64 ", remote id 0x" ChipLogFormatX64, ChipLogValueX64(localId),
                    ChipLogValueX64(remoteId));

    initParams.storageDelegate = &mStorage;

    err = mOpCredsIssuer.Initialize(mStorage);
    VerifyOrExit(err == CHIP_NO_ERROR, ChipLogError(Controller, "Init failure! Operational Cred Issuer: %s", chip::ErrorStr(err)));

    initParams.operationalCredentialsDelegate = &mOpCredsIssuer;

    err = mController.SetUdpListenPort(mStorage.GetListenPort());
    VerifyOrExit(err == CHIP_NO_ERROR, ChipLogError(Controller, "Init failure! Commissioner: %s", chip::ErrorStr(err)));

    VerifyOrExit(rcac.Alloc(chip::Controller::kMaxCHIPDERCertLength), err = CHIP_ERROR_NO_MEMORY);
    VerifyOrExit(noc.Alloc(chip::Controller::kMaxCHIPDERCertLength), err = CHIP_ERROR_NO_MEMORY);
    VerifyOrExit(icac.Alloc(chip::Controller::kMaxCHIPDERCertLength), err = CHIP_ERROR_NO_MEMORY);

    {
        chip::MutableByteSpan nocSpan(noc.Get(), chip::Controller::kMaxCHIPDERCertLength);
        chip::MutableByteSpan icacSpan(icac.Get(), chip::Controller::kMaxCHIPDERCertLength);
        chip::MutableByteSpan rcacSpan(rcac.Get(), chip::Controller::kMaxCHIPDERCertLength);

        chip::Crypto::P256Keypair ephemeralKey;
        SuccessOrExit(err = ephemeralKey.Initialize());

        // TODO - OpCreds should only be generated for pairing command
        //        store the credentials in persistent storage, and
        //        generate when not available in the storage.
        err = mOpCredsIssuer.GenerateNOCChainAfterValidation(localId, 0, ephemeralKey.Pubkey(), rcacSpan, icacSpan, nocSpan);
        SuccessOrExit(err);

        initParams.ephemeralKeypair = &ephemeralKey;
        initParams.controllerRCAC   = rcacSpan;
        initParams.controllerICAC   = icacSpan;
        initParams.controllerNOC    = nocSpan;

        err = mController.Init(initParams);
        VerifyOrExit(err == CHIP_NO_ERROR, ChipLogError(Controller, "Init failure! Commissioner: %s", chip::ErrorStr(err)));
    }

#if CONFIG_USE_SEPARATE_EVENTLOOP
    // ServiceEvents() calls StartEventLoopTask(), which is paired with the
    // StopEventLoopTask() below.
    err = mController.ServiceEvents();
    VerifyOrExit(err == CHIP_NO_ERROR, ChipLogError(Controller, "Init failure! Run Loop: %s", chip::ErrorStr(err)));
#endif // CONFIG_USE_SEPARATE_EVENTLOOP

#ifdef CONFIG_CHIP_TOOL_SERVER
    while (true)
    {
        if (access(FIFO_PATH, F_OK) != 0)
        {
            mkfifo(FIFO_PATH, 0666);
        }
        fd = open(FIFO_PATH, O_RDONLY);
        if (fd >= 0)
        {
            while ((ret = read(fd, read_buf, FIFO_BUF_SZ) > 0))
            {
            }
            close(fd);
        }
        for (int i = 0; i < 20; i++)
        {
            args[i] = NULL;
        }
        args_pass = args;
        argn      = 0;
        delim     = (char *) space_char;
        split_ptr = strtok(read_buf, delim);
        while (split_ptr != NULL)
        {
            args[argn] = split_ptr;
            argn++;
            split_ptr = strtok(NULL, delim);
        }
        ChiptoolCommandOptions::GetInstance().DeviceName.assign("");
        if (CHIP_NO_ERROR == ParseArguments(argn, args_pass))
        {
            if (ChiptoolCommandOptions::GetInstance().DeviceName.length() != 0)
            {
                char * processName = args_pass[0];
                args_pass += 2;
                *args_pass = processName;
                argn -= 2;
                mStorage.mSectionName.assign(ChiptoolCommandOptions::GetInstance().DeviceName);
                localId  = mStorage.GetLocalNodeId();
                remoteId = mStorage.GetRemoteNodeId();
                ChiptoolCommandOptions::GetInstance().nodeDict.insert(
                    { remoteId, ChiptoolCommandOptions::GetInstance().DeviceName });
                ChipLogError(Controller, "remoteId(0x%lx) assigned to DeviceName:%s", remoteId,
                             ChiptoolCommandOptions::GetInstance().nodeDict[remoteId].c_str());
            }
        }
        err = RunCommand(localId, remoteId, argn, args_pass, &command);
        command->Shutdown();
    }
#else
    err = RunCommand(localId, remoteId, argc, argv, &command);
    SuccessOrExit(err);
    // Loop here will be OK
    // err = RunCommand(localId, remoteId, argc, argv, &command);
    // SuccessOrExit(err);
#endif

#if !CONFIG_USE_SEPARATE_EVENTLOOP
    chip::DeviceLayer::PlatformMgr().RunEventLoop();
#endif // !CONFIG_USE_SEPARATE_EVENTLOOP

exit:
#if CONFIG_USE_SEPARATE_EVENTLOOP
    chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
#endif // CONFIG_USE_SEPARATE_EVENTLOOP

    if ((err == CHIP_NO_ERROR) && (command != nullptr))
    {
        err = command->GetCommandExitStatus();
    }
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(chipTool, "Run command failure: %s", chip::ErrorStr(err));
    }

    if (command)
    {
        command->Shutdown();
    }

    //
    // We can call DeviceController::Shutdown() safely without grabbing the stack lock
    // since the CHIP thread and event queue have been stopped, preventing any thread
    // races.
    //
    mController.Shutdown();

    return (err == CHIP_NO_ERROR) ? EXIT_SUCCESS : EXIT_FAILURE;
}

CHIP_ERROR Commands::RunCommand(NodeId localId, NodeId remoteId, int argc, char ** argv, Command ** ranCommand)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    std::map<std::string, CommandsVector>::iterator cluster;
    Command * command = nullptr;
#ifdef CONFIG_CHIP_TOOL_MANAGER
    int fd;
    char write_buf[FIFO_BUF_SZ] = { 0 };
    int i;
#endif

    if (argc <= 1)
    {
        ChipLogError(chipTool, "Missing cluster name");
        ShowClusters(argv[0]);
        ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    cluster = GetCluster(argv[1]);
    if (cluster == mClusters.end())
    {
        ChipLogError(chipTool, "Unknown cluster: %s", argv[1]);
        ShowClusters(argv[0]);
        ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    if (argc <= 2)
    {
        ChipLogError(chipTool, "Missing command name");
        ShowCluster(argv[0], argv[1], cluster->second);
        ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    if (!IsGlobalCommand(argv[2]))
    {
        command = GetCommand(cluster->second, argv[2]);
        if (command == nullptr)
        {
            ChipLogError(chipTool, "Unknown command: %s", argv[2]);
            ShowCluster(argv[0], argv[1], cluster->second);
            ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
        }
    }
    else
    {
        if (argc <= 3)
        {
            ChipLogError(chipTool, "Missing attribute name");
            ShowClusterAttributes(argv[0], argv[1], argv[2], cluster->second);
            ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
        }

        command = GetGlobalCommand(cluster->second, argv[2], argv[3]);
        if (command == nullptr)
        {
            ChipLogError(chipTool, "Unknown attribute: %s", argv[3]);
            ShowClusterAttributes(argv[0], argv[1], argv[2], cluster->second);
            ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
        }
    }

    if (!command->InitArguments(argc - 3, &argv[3]))
    {
        ShowCommand(argv[0], argv[1], command);
        ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
    }
#ifdef CONFIG_CHIP_TOOL_MANAGER
    if (access(FIFO_PATH, F_OK) == 0)
    {

        char * dev_name = NULL;
        if (ChiptoolCommandOptions::GetInstance().DeviceName.length() != 0)
        {
            dev_name = (char *) ChiptoolCommandOptions::GetInstance().DeviceName.c_str();
        }
        for (i = 0; i < argc; i++)
        {
            if (i == 1 && dev_name != NULL)
            {
                strcat(write_buf, "--name");
                strcat(write_buf, " ");
                strcat(write_buf, dev_name);
                strcat(write_buf, " ");
            }
            strcat(write_buf, argv[i]);
            strcat(write_buf, " ");
        }
        fd = open(FIFO_PATH, O_WRONLY);

        if (fd >= 0)
        {
            write(fd, write_buf, sizeof(write_buf));
            close(fd);
        }
        else
        {
            ChipLogError(chipTool, "failed to open FIFO file:%s", FIFO_PATH);
        }
    }

#else
    {
        Command::ExecutionContext execContext;

        execContext.commissioner  = &mController;
        execContext.opCredsIssuer = &mOpCredsIssuer;
        execContext.storage       = &mStorage;
        execContext.localId       = localId;
        execContext.remoteId      = remoteId;

        command->SetExecutionContext(execContext);
        *ranCommand = command;

        //
        // Set this to true first BEFORE we send commands to ensure we don't end
        // up in a situation where the response comes back faster than we can
        // set the variable to true, which will cause it to block indefinitely.
        //
        command->UpdateWaitForResponse(true);
#if CONFIG_USE_SEPARATE_EVENTLOOP
        chip::DeviceLayer::PlatformMgr().ScheduleWork(RunQueuedCommand, reinterpret_cast<intptr_t>(command));
        command->WaitForResponse(command->GetWaitDurationInSeconds());
#else  // CONFIG_USE_SEPARATE_EVENTLOOP
        err = command->Run();
        SuccessOrExit(err);
        command->ScheduleWaitForResponse(command->GetWaitDurationInSeconds());
#endif // CONFIG_USE_SEPARATE_EVENTLOOP
    }
#endif

exit:
    return err;
}

void Commands::RunQueuedCommand(intptr_t commandArg)
{
    auto * command = reinterpret_cast<Command *>(commandArg);
    CHIP_ERROR err = command->Run();
    if (err != CHIP_NO_ERROR)
    {
        command->SetCommandExitStatus(err);
    }
}

std::map<std::string, Commands::CommandsVector>::iterator Commands::GetCluster(std::string clusterName)
{
    for (auto & cluster : mClusters)
    {
        std::string key(cluster.first);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key.compare(clusterName) == 0)
        {
            return mClusters.find(cluster.first);
        }
    }

    return mClusters.end();
}

Command * Commands::GetCommand(CommandsVector & commands, std::string commandName)
{
    for (auto & command : commands)
    {
        if (commandName.compare(command->GetName()) == 0)
        {
            return command.get();
        }
    }

    return nullptr;
}

Command * Commands::GetGlobalCommand(CommandsVector & commands, std::string commandName, std::string attributeName)
{
    for (auto & command : commands)
    {
        if (commandName.compare(command->GetName()) == 0 && attributeName.compare(command->GetAttribute()) == 0)
        {
            return command.get();
        }
    }

    return nullptr;
}

bool Commands::IsGlobalCommand(std::string commandName) const
{
    return commandName.compare("read") == 0 || commandName.compare("write") == 0 || commandName.compare("report") == 0;
}

void Commands::ShowClusters(std::string executable)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [--name section_name] cluster_name command_name [param1 param2 ...]\n", executable.c_str());
    fprintf(stderr, "\n");
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
    fprintf(stderr, "  | Clusters:                                                                           |\n");
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
    for (auto & cluster : mClusters)
    {
        std::string clusterName(cluster.first);
        std::transform(clusterName.begin(), clusterName.end(), clusterName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        fprintf(stderr, "  | * %-82s|\n", clusterName.c_str());
    }
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
}

void Commands::ShowCluster(std::string executable, std::string clusterName, CommandsVector & commands)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [--name section_name] %s command_name [param1 param2 ...]\n", executable.c_str(), clusterName.c_str());
    fprintf(stderr, "\n");
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
    fprintf(stderr, "  | Commands:                                                                           |\n");
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
    bool readCommand   = false;
    bool writeCommand  = false;
    bool reportCommand = false;
    for (auto & command : commands)
    {
        bool shouldPrint = true;

        if (IsGlobalCommand(command->GetName()))
        {
            if (strcmp(command->GetName(), "read") == 0 && readCommand == false)
            {
                readCommand = true;
            }
            else if (strcmp(command->GetName(), "write") == 0 && writeCommand == false)
            {
                writeCommand = true;
            }
            else if (strcmp(command->GetName(), "report") == 0 && reportCommand == false)
            {
                reportCommand = true;
            }
            else
            {
                shouldPrint = false;
            }
        }

        if (shouldPrint)
        {
            fprintf(stderr, "  | * %-82s|\n", command->GetName());
        }
    }
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
}

void Commands::ShowClusterAttributes(std::string executable, std::string clusterName, std::string commandName,
                                     CommandsVector & commands)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s %s %s attribute-name [param1 param2 ...]\n", executable.c_str(), clusterName.c_str(),
            commandName.c_str());
    fprintf(stderr, "\n");
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
    fprintf(stderr, "  | Attributes:                                                                         |\n");
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
    for (auto & command : commands)
    {
        if (commandName.compare(command->GetName()) == 0)
        {
            fprintf(stderr, "  | * %-82s|\n", command->GetAttribute());
        }
    }
    fprintf(stderr, "  +-------------------------------------------------------------------------------------+\n");
}

void Commands::ShowCommand(std::string executable, std::string clusterName, Command * command)
{
    fprintf(stderr, "Usage:\n");

    std::string arguments = "";
    arguments += command->GetName();

    size_t argumentsCount = command->GetArgumentsCount();
    for (size_t i = 0; i < argumentsCount; i++)
    {
        arguments += " ";
        arguments += command->GetArgumentName(i);
    }
    fprintf(stderr, "  %s %s %s\n", executable.c_str(), clusterName.c_str(), arguments.c_str());
}
