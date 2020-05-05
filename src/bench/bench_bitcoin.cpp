// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"

#include "allowed_args.h"
#include "bench/bench_constants.h"
#include "crypto/sha256.h"
#include "key.h"
#include "main.h"
#include "rpc/client.h"
#include "sync.h"
#include "util.h"

#include <boost/lexical_cast.hpp>
#include <memory>

int main(int argc, char *argv[])
{
    try
    {
        std::string appname("bench_bitcoin");
        std::string usage = "\n" + std::string("Usage:") + "\n" + "  " + appname + " [options] " + "\n";
        int ret = AppInitRPC(usage, AllowedArgs::BitcoinBench(), argc, argv);
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
}
