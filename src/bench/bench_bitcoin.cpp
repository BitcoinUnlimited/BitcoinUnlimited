// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"

#include "allowed_args.h"
#include "crypto/sha256.h"
#include "key.h"
#include "main.h"
#include "rpc/client.h"
#include "sync.h"
#include "util.h"

#include <boost/lexical_cast.hpp>
#include <memory>

static const int64_t DEFAULT_BENCH_EVALUATIONS = 5;
static const char *DEFAULT_BENCH_FILTER = ".*";
static const char *DEFAULT_BENCH_SCALING = "1.0";
static const char *DEFAULT_BENCH_PRINTER = "console";
static const char *DEFAULT_PLOT_PLOTLYURL = "https://cdn.plot.ly/plotly-latest.min.js";
static const int64_t DEFAULT_PLOT_WIDTH = 1024;
static const int64_t DEFAULT_PLOT_HEIGHT = 768;


class BitcoinBenchArgs : public AllowedArgs::BitcoinCli
{
public:
    BitcoinBenchArgs(CTweakMap *pTweaks = nullptr)
    {
        addHeader("Bitcoin Bench options:")
            .addArg("-list", ::AllowedArgs::optionalStr,
                "List benchmarks without executing them. Can be combined with -scaling and -filter")
            .addArg("-evals=<n>", ::AllowedArgs::requiredInt,
                strprintf("Number of measurement evaluations to perform. (default: %u)", DEFAULT_BENCH_EVALUATIONS))
            .addArg("-filter=<regex>", ::AllowedArgs::requiredInt,
                strprintf("Regular expression filter to select benchmark by name (default: %s)", DEFAULT_BENCH_FILTER))
            .addArg("-scaling=<n>", ::AllowedArgs::requiredInt,
                strprintf("Scaling factor for benchmark's runtime (default: %u)", DEFAULT_BENCH_SCALING))
            .addArg("-printer=(console|plot)", ::AllowedArgs::requiredStr,
                strprintf("Choose printer format. console: print data to console. plot: Print results as HTML graph "
                          "(default: %s)",
                        DEFAULT_BENCH_PRINTER))
            .addArg("-plot-plotlyurl=<uri>", ::AllowedArgs::requiredInt,
                strprintf("URL to use for plotly.js (default: %s)", DEFAULT_PLOT_PLOTLYURL))
            .addArg("-plot-width=<x>", ::AllowedArgs::requiredInt,
                strprintf("Plot width in pixel (default: %u)", DEFAULT_PLOT_WIDTH))
            .addArg("-plot-height=<x>", ::AllowedArgs::requiredInt,
                strprintf("Plot height in pixel (default: %u)", DEFAULT_PLOT_HEIGHT));
    }
};

int main(int argc, char *argv[])
{
    try
    {
        std::string appname("bench_bitcoin");
        std::string usage = "\n" + std::string("Usage:") + "\n" + "  " + appname + " [options] " + "\n";
        int ret = AppInitRPC(usage, BitcoinBenchArgs(), argc, argv);
        if (ret != CONTINUE_EXECUTION)
            return ret;
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "AppInitRPC()");
        return EXIT_FAILURE;
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    SHA256AutoDetect();
    RandomInit();
    ECC_Start();
    SetupEnvironment();
    fPrintToDebugLog = false; // don't want to write to debug.log file

    int64_t evaluations = GetArg("-evals", DEFAULT_BENCH_EVALUATIONS);
    std::string regex_filter = GetArg("-filter", DEFAULT_BENCH_FILTER);
    std::string scaling_str = GetArg("-scaling", DEFAULT_BENCH_SCALING);
    bool is_list_only = GetBoolArg("-list", false);

    double scaling_factor = boost::lexical_cast<double>(scaling_str);


    std::unique_ptr<benchmark::Printer> printer(new benchmark::ConsolePrinter());
    std::string printer_arg = GetArg("-printer", DEFAULT_BENCH_PRINTER);
    if ("plot" == printer_arg)
    {
        printer.reset(new benchmark::PlotlyPrinter(GetArg("-plot-plotlyurl", DEFAULT_PLOT_PLOTLYURL),
            GetArg("-plot-width", DEFAULT_PLOT_WIDTH), GetArg("-plot-height", DEFAULT_PLOT_HEIGHT)));
    }

    benchmark::BenchRunner::RunAll(*printer, evaluations, scaling_factor, regex_filter, is_list_only);

    ECC_Stop();
}
