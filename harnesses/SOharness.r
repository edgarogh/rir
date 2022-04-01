
JitDisabled <- Sys.getenv(x = "PIR_DISABLE_COMPILATION")

verifyResult <- function(x, ...) {
    UseMethod("verifyResult", x)
}

verifyResult.default <- function(result, benchmarkParameter) {
    TRUE
}

innerBenchmarkLoop <- function(x, ...) {
    UseMethod("innerBenchmarkLoop", x)
}

innerBenchmarkLoop.default <- function(class, benchmarkParameter, innerIterations) {
    for (i in 1:innerIterations) {
        if (!verifyResult(execute(benchmarkParameter), benchmarkParameter)) {
            return(FALSE)
        }
    }
    return(TRUE)
}

doRuns <- function(name, iterations, benchmarkParameter, innerIterations) {
    total <- 0
    class(name) <- tolower(name)
    path <- paste("/opt/rir/bitcodes/", gsub("/","_",name), "/", sep ="")
    Sys.setenv(PIR_DESERIALIZE_PREFIX = path)

    JitDisabled <- Sys.getenv(x = "PIR_DISABLE_COMPILATION")

    startTime <- Sys.time()
    if (JitDisabled == "1") {
        f.loadBitcodes()
    } else {
        Sys.setenv(PIR_DISABLE_COMPILATION = "1")
        f.loadBitcodes()
        Sys.unsetenv("PIR_DISABLE_COMPILATION")
    }
    endTime <- Sys.time()
    loadTime <- (as.numeric(endTime) - as.numeric(startTime)) * 1000000

    for (i in 1:iterations) {
        startTime <- Sys.time()

        if (!innerBenchmarkLoop(name, benchmarkParameter, innerIterations)) {
            stop("Benchmark failed with incorrect result")
        }

        endTime <- Sys.time()
        runTime <- (as.numeric(endTime) - as.numeric(startTime)) * 1000000

        if (i == 1) {
            runTime = runTime + loadTime
        }

        cat(name, ": iterations=1 runtime: ", round(runTime), "us\n", sep = "")
        total <- total + runTime
    }
    total
}

run <- function(args) {
    if (length(args) < 3 || 4 < length(args))
        stop(printUsage())

    name <- args[[1]]
    numIterations <- strtoi(args[[2]])
    benchmarkParameter <- strtoi(args[[3]])
    innerIterations <- if (length(args) > 3) strtoi(args[[4]]) else 1

    source(file.path(".", paste(tolower(name), ".r", sep="")))

    total <- as.numeric(doRuns(name, numIterations, benchmarkParameter, innerIterations));
    cat(name, ": ",
        "iterations=", numIterations, "; ",
        "average: ", round(total / numIterations), " us; ",
        "total: ", round(total), "us\n\n", sep="")
    # stopifnot(FALSE)
    #cat("Total runtime: ", total, "us\n\n", sep="")
}

printUsage <- function() {
    cat("harness.r benchmark num-iterations benchmark-parameter [inner-iter]\n")
    cat("\n")
    cat("  benchmark           - benchmark class name\n")
    cat("  num-iterations      - number of times to execute benchmark\n")
    cat("  benchmark-parameter - size of the benchmark problem\n")
    cat("  inner-iter          - number of times the benchmark is executed in an inner loop,\n")
    cat("                        which is measured in total, default: 1\n")
}

run(commandArgs(trailingOnly=TRUE))
