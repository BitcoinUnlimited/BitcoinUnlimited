// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "chainparams.h"
#include "clientversion.h"
#include "fs.h"
#include "rpc/server.h"
#include "init.h"
#include "noui.h"
#include "scheduler.h"
#include "util.h"
#include "httpserver.h"
#include "httprpc.h"
#include "utilstrencodings.h"
#include "unlimited.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/thread.hpp>

#include <stdio.h>

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called Bitcoin (https://www.bitcoin.org/),
 * which enables instant payments to anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

static bool fDaemon;

void WaitForShutdown(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown.
    while (!fShutdown)
    {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
    if (threadGroup)
    {
        Interrupt(*threadGroup);
        threadGroup->join_all();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[])
{
    boost::thread_group threadGroup;
    CScheduler scheduler;

    bool fRet = false;

    //
    // Parameters
    //
    // If Qt is used, parameters/bitcoin.conf are parsed in qt/bitcoin.cpp's main()
    AllowedArgs::Bitcoind allowedArgs(&tweaks);
    try {
        ParseParameters(argc, argv, allowedArgs);
    } catch (const std::exception& e) {
        fprintf(stderr, "Error parsing program options: %s\n", e.what());
        return false;
    }

    // Process help and version before taking care about datadir
    if (mapArgs.count("-?") || mapArgs.count("-h") ||  mapArgs.count("-help") || mapArgs.count("-version"))
    {
        std::string strUsage = strprintf(_("%s Daemon"), _(PACKAGE_NAME)) + " " + _("version") + " " + FormatFullVersion() + "\n";

        if (mapArgs.count("-version"))
        {
            strUsage += FormatParagraph(LicenseInfo());
        }
        else
        {
            strUsage += "\n" + _("Usage:") + "\n" +
                  "  bitcoind [options]                     " + strprintf(_("Start %s Daemon"), _(PACKAGE_NAME)) + "\n";

            strUsage += "\n" + allowedArgs.helpMessage();
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return true;
    }

    try
    {
        if (!fs::is_directory(GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
            return false;
        }
        try
        {
            ReadConfigFile(mapArgs, mapMultiArgs, allowedArgs);
        } catch (const std::exception& e) {
            fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try {
            SelectParams(ChainNameFromCommandLine());
        } catch (const std::exception& e) {
            fprintf(stderr, "Error: %s\n", e.what());
            return false;
        }

        // Command-line RPC
        bool fCommandLine = false;
        for (int i = 1; i < argc; i++)
            if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "bitcoin:"))
                fCommandLine = true;

        if (fCommandLine)
        {
            fprintf(stderr, "Error: There is no RPC client functionality in bitcoind anymore. Use the bitcoin-cli utility instead.\n");
            return false;
        }
#ifndef WIN32
        fDaemon = GetBoolArg("-daemon", false);
        if (fDaemon)
        {
            fprintf(stdout, "Bitcoin server starting\n");

            // Daemonize
            pid_t pid = fork();
            if (pid < 0)
            {
                fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
                return false;
            }
            if (pid > 0) // Parent process, pid is child process id
            {
                return true;
            }
            // Child process falls through to rest of initialization

            pid_t sid = setsid();
            if (sid < 0)
                fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
        }
#endif
        SoftSetBoolArg("-server", true);

        // Set this early so that parameter interactions go to console
        InitLogging();
        InitParameterInteraction();
        fRet = AppInit2(threadGroup, scheduler);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInit()");
    }

    UnlimitedSetup();

    if (!fRet)
    {
        Interrupt(threadGroup);
        // threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
        // the startup-failure cases to make sure they don't result in a hang due to some
        // thread-blocking-waiting-for-another-thread-during-startup case
    } else {
        WaitForShutdown(&threadGroup);
    }
    Shutdown();

    return fRet;
}

void VDHlogtest();
void VDHtest();
int main(int argc, char* argv[])
{
    SetupEnvironment();
    fPrintToConsole=true;
    //fPrintToDebugLog=true;	LOGA("missing args %s %d\n");
	LOGA("wrong order args %s %d\n",3,"hello");
	LOGA("null arg %s\n",NULL);

    fDebug=true;

    //VDHlogtest();
    VDHtest();
    //return 0;

    // Connect bitcoind signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE);
}

using namespace std;

#if 0

call IsStringTrue() with various inputs and check the output.
call LOG(weird stuff) a bunch of times to exercise it.
call setlog(const UniValue &params, bool fHelp) with a few params and verify categoriesEnabled. Call setlog with garbage categories
#endif
UniValue setlog(const UniValue &params, bool fHelp);

bool TestSetLog(uint64_t categoriesExpected,const char* arg1, const char* arg2 = NULL)
{
	UniValue logargs(UniValue::VARR);
	bool ret = false;
	logargs.push_back(arg1);
	if(arg2 != NULL)
		logargs.push_back(arg2);

	setlog(logargs,false); // The function to be tested

	if(categoriesExpected==Logging::categoriesEnabled)
		ret = true;

	LOGA("TestSetLog %s %s ret: %d\n", arg1, ((arg2 == NULL)?"":arg2),(int)ret);
	return ret;
}


bool IsStringTrueBadArgTest(const char* arg1)
{
	try
	{
		IsStringTrue(arg1);
	}
	catch(...)
	{
		return true;// If bad arg return true
	}
	return false;
}

using namespace Logging;
void VDHtest()
{
	//string s = LogGetAllString();
	//const char* foo ="foo";
	//printf(foo);
	assert(sizeof(categoriesEnabled) == 8);

	LogInit();
	LogToggleCategory(THN,true);
	LOGA("test 0x%llx\n", 99);
	LOGA("test\n");

	LOGA("test 0x%llx\n", 99);
	LOGA("mask: %s 0x%llx\n", 99);
	LOGA("mask:  \n");
	LOG(THN,"THN %d\n", 1);
	LogToggleCategory(THN,true);
	LOG(THN|TOR,"THN|TOR %d\n", 2);
	

	//Weird stuff:
	//LOG(THN,NULL);
	LOG(THN,"missing args %s %d\n");
	LOG(THN,"wrong order args %s %d\n",3,"hello");
	LOG(THN,"null arg %s\n",NULL);
	


	//LOGA(NULL);
	
	IsStringTrue("true");
	IsStringTrue("enable");
	IsStringTrue("1");
	IsStringTrue("on");
	
	IsStringTrue("false");
	IsStringTrue("disable");
	IsStringTrue("0");
	IsStringTrue("off");
	//IsStringTrueBadArgTest("bad");



	TestSetLog(ALL,"all","on");
	TestSetLog(NONE,"all","off");
	TestSetLog(NONE,"tor");
	TestSetLog(TOR,"tor","on");
	TestSetLog(NONE,"tor","off");
	TestSetLog(TOR,"tor","on-JunkArg"); //junk in stead of "on"

	TestSetLog(categoriesEnabled,"badcat","on");
}


#if 0

void LogGetAllStringTest()
{
	//string s = LogGetAllString();
	//const char* foo ="foo";
	//printf(foo);

	LOGA("mask:  0x%llx\n", 99);
	LOGA("mask: %s 0x%llx\n", 99);
	LOGA("mask:  \n");
	LOG(THN,"THN %d\n", (int)1);
	Logging::LogToggleCategory(Logging::THN,true);
	LOG(THN|TOR,"THN|TOR %d\n", (int)2);

	//printf("foo%s");
}


template<typename T1, typename... Args>
void LogWriteFOO(uint64_t category, const char* fmt, const T1& v1, const Args&... args)
{
	if(ALL==category || LogAcceptCategory(category))
    	LogWriteStr(LogGetLabel(category)+": "+tfm::format(fmt, v1, args...));
}




/**
 * Get a "true" or "false" string for a category.
 * @param[in] category
 * returns state
 */
std::string LogGetCategoryState(std::string category)
{
	category = boost::algorithm::to_upper_copy(category);
	uint64_t catg = LogFindCategory(category);
	string state = LogAcceptCategory(catg)?"true":"false";

	LOGA("category \"%s\" state \"%s\"\n", category,state);
	return state;
}
#endif




#if 0
void LogCmdTest(string lgs, string cmd, string cmdarg)
{
	bool turnon = IsStringTrue(cmdarg);
	cmd = boost::algorithm::to_lower_copy(cmd);
	uint64_t catg = LogFindCategory(cmd);

	if(cmd == "all")
	{
		if(turnon)
			LogTurnOnAll();
		else
			LogTurnOffAll();

		LOGA("ALL mask:  0x%llx\n", categoriesEnabled);
		LogCategoryState(cmd);
	}
	else
	if(turnon)
	{
		LogSetCategory(catg);
		LOGA("mask:  0x%llx\n", categoriesEnabled);
		LogCategoryState(cmd);
	}
	else
	{
		LogRmCategory(THN);
		LOGA("mask:  0x%llx\n", categoriesEnabled);
		LogCategoryState(cmd);
	}

}

void LogCmdTest()
{
	categoriesEnabled = 0;

	LogCmdTest("On: ","thin","1");
	LogCmdTest("Off: ","thin","off");

	LogCmdTest("On: ","all","enable");
	LogCmdTest("Off: ","all","disable");
}





void VDHtest()
{

	bool test = false;
	const char* str = "junk";

	try{
		test = IsStringTrue("1");
		LOGA("IsStringTrue() %d\n", test);

		test = IsStringTrue("OfF");
		LOGA("IsStringTrue() %d\n", test);

		test = IsStringTrue(str);
		LOGA("NEVER happen IsStringTrue() %d\n", test);
	}catch(...)
	{
		LOGA("catch IsStringTrue() %s\n",str);
	}

	string label="horken";
	label = LogGetLabel(THN);
	LOGA("LogGetLabel() %s\n",label);

	label = LogGetLabel(99);
		LOGA("LogGetLabel()junk %s\n",label);
}
#endif


//#undef LOGA
//#define LOGA(...) Logging::LogWrite(Logging::ALL,__VA_ARGS__)
void VDHlogtestAA()
{
	int yes =3;
	LOGA("LOGA\n");


	LOG(THN,"THN %d\n", (int)yes);
	LOG(THN|TOR,"THN|TOR %d\n", (int)yes);

}



using namespace Logging;

//#define logMask categoriesEnabled
//#define LogSetId LogSetCategory
//#define LogRmLogId LogRmCategory
//#define LogAcceptLogId LogAcceptCategory
//#define LogPrint LogWrite
//

void VDHlogtest()
{
	//bool yes = false;
	//categoriesEnabled= 0UL;

	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(THN,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(THN,false);
	LOGA("mask:  0x%llx\n", categoriesEnabled);


	LogToggleCategory(THN|TOR,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(THN,false);
	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(THN|TOR,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);

	LogToggleCategory(ALL,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(ALL,false);
	LOGA("mask:  0x%llx\n", categoriesEnabled);

}
#if 0
void VDHlogtest()
{
	bool yes = false;
	//categoriesEnabled= 0UL;

	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(THN);
	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(THN,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);


	LogToggleCategory(THN|TOR);
	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(THN,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(THN|TOR);
	LOGA("mask:  0x%llx\n", categoriesEnabled);
	LogToggleCategory(ALL,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);


	LogToggleCategory(ALL);
	LOGA("mask:  0x%llx\n", categoriesEnabled);

	LogToggleCategory(THN,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);

	LogToggleCategory(ALL,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);

	LogToggleCategory(THN|TOR,true);
	LOGA("mask:  0x%llx\n", categoriesEnabled);

	LogRmAllCategories();
	LOGA("mask:  0x%llx\n", categoriesEnabled);





	//categoriesEnabled = categoriesEnabled + THIN;
	LogToggleCategory(THN);
	yes =  LogAcceptCategory(THN);
	LOGA("THIN on: %d\n", (int)yes);

	LogToggleCategory(THN);
	yes =  LogAcceptCategory(THN);
	LOGA("THIN on: %d\n", (int)yes);

	yes = LogAcceptCategory(TOR);
	LOGA("TOR on: %d\n", (int)yes);
	categoriesEnabled = categoriesEnabled | TOR;
	LOGA("TOR #2 on: %d\n", (int)yes);

	yes = LogAcceptCategory(ALL);
	LOGA("ANY on: %d\n", (int)yes);

	LOG(ALL,"MASK ANY on: %d\n", (int)yes);
	//LOG("rpc","old on: %d\n", (int)yes);

#ifdef FINDLABEL
	std::string label = logLabelMap[THIN];
	LOGA("LOGTHIN label: %s\n", label);

	uint64_t cat= LogFindLogId(label);

	if(cat!=THIN)
		LOGA("LogFindLogId broken");

	if(cat!=THIN)
			LOGA("LogFindLogId broken");

	LOG(99,"MASK bad ID %d\n", (int)yes);
#endif

	//VDHlogtestBB();
	fDebug = true;
	LogToggleCategory(THN);
	LOG(THN,"END2\n");

	//----------
	LogRmAllCategories();
	LogToggleCategory(THN);
	LOG(THN,"THN\n");
	LOG(TOR,"TOR <<- should not log\n");
	LOG(THN|TOR,"THN|TOR\n");
	LogToggleCategory(TOR);
	LOG(THN,"THN\n");
	LOG(TOR,"TOR\n");
	LOG(THN|TOR,"THN|TOR\n");
	LogToggleCategory(THN);
	LOG(THN,"~THN  <<- should not log \n");
	LOGA("END3\n");
	//LogPrintStr("foo");

	//LogFindLogId(label);

	//LogPrintMask(THN,"LOG THIN label: %s\n", label);

}


#endif














