#include "crash.h"

#include "sdl_wrappers.h"

#if defined(BACKTRACE)

#    include <atomic>
#    include <csignal>
#    include <cstdio>
#    include <cstdlib>
#    include <exception>
#    include <initializer_list>
#    include <iostream>
#    include <sstream>
#    include <string>
#    include <typeinfo>

#    if !defined(_WIN32)
#        include <execinfo.h>
#        include <unistd.h>
#        include <cstring>
#    endif

#    if defined(_WIN32)
#        if 1 // HACK: Hack to prevent reordering of #include "platform_win.h" by IWYU
#            include "platform_win.h"
#        endif
#        include <dbghelp.h>
#    endif

#    include "cached_options.h"
#    include "debug.h"
#    include "driver_window.h"
#    include "get_version.h"
#    include "path_info.h"

// signal handlers are expected to have C linkage, and only use the
// common subset of C & C++

#    if defined(_WIN32)
// Ensures only the first crash path (exception filter or signal handler) logs and dumps.
static std::atomic_flag g_crash_logged{};
// Set by windows_exception_filter; used by dump_to() and debug_write_backtrace().
static EXCEPTION_POINTERS* g_exception_info = nullptr;
#    endif

extern "C" {

#    if defined(_WIN32)
static void dump_to(const char* file) {
    HANDLE handle =
        CreateFile(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    MINIDUMP_EXCEPTION_INFORMATION mdei = {};
    MINIDUMP_EXCEPTION_INFORMATION* mdei_ptr = nullptr;
    if (g_exception_info) {
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = g_exception_info;
        mdei.ClientPointers = FALSE;
        mdei_ptr = &mdei;
    }
    MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), handle,
        static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithUnloadedModules), mdei_ptr, nullptr,
        nullptr);
    CloseHandle(handle);
}
#    endif

#    if !defined(_WIN32)
/// Writes `text` to stderr with write(2) alone: no allocation, no stdio, safe in a signal handler.
static void write_stderr(const char* text) {
    if (write(STDERR_FILENO, text, std::strlen(text)) < 0) { return; }
}

/// The crash report that is safe from inside the allocator: static strings and
/// backtrace_symbols_fd, nothing that allocates. Always written first, before the full report.
static void write_safe_report(const char* msg) {
    write_stderr("\nThe program has crashed.\nMESSAGE: ");
    write_stderr(msg);
    write_stderr("\nSTACK TRACE (raw frames):\n");
    void* frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
}
#    endif

static void log_crash(const char* type, const char* msg) {
    // This implementation is not technically async-signal-safe for many
    // reasons, including the memory allocations and the SDL message box.
    // But it should usually work in practice, unless for example the
    // program segfaults inside malloc.

    // Flush the debug log before writing the crash report so that any
    // messages written just before the crash are on disk.
    flush_debug_log();

    const std::string crash_log_file = PATH_INFO::crash();
#    if defined(_WIN32)
    const std::string minidump_file = crash_log_file + ".dmp";
    dump_to(minidump_file.c_str());
#    endif
    std::ostringstream log_text;
    log_text << "The program has crashed."
             << "\nSee the log file for a stack trace."
             << "\nCRASH LOG FILE: " << crash_log_file
#    if defined(_WIN32)
             << "\nMINIDUMP FILE:  " << minidump_file
             << "\n(Attach both files when reporting this crash)"
#    endif
             << "\nVERSION: " << getVersionString() << "\nTYPE: " << type << "\nMESSAGE: " << msg;
    // A driver window or a test run has nobody at the keyboard: a modal box would hold the
    // crashed process (and a dialog on the user's desktop) until a watchdog kills it, so a
    // crashing test hung its runner instead of failing. The report below goes to stderr and
    // the crash file as usual.
    if (!requested_driver_window() && !test_mode
        && SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", log_text.str().c_str(), nullptr)
               != 0) {
        log_text << "Error creating SDL message box: " << SDL_GetError() << '\n';
    }
    log_text << "\nSTACK TRACE:\n";
    debug_write_backtrace(log_text);
    std::cerr << log_text.str();
    FILE* file = fopen(crash_log_file.c_str(), "w");
    if (file) {
        fwrite(log_text.str().data(), 1, log_text.str().size(), file);
        fclose(file);
    }
}

static void signal_handler(int sig) {
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wunknown-pragmas"
#    pragma GCC diagnostic ignored "-Wold-style-cast"
#    pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
    signal(sig, SIG_DFL);
#    pragma GCC diagnostic pop
#    if defined(_WIN32)
    if (g_crash_logged.test_and_set()) {
        raise(sig);
        return;
    }
#    endif
    const char* msg;
    switch (sig) {
        case SIGSEGV:
            msg = "SIGSEGV: Segmentation fault";
            break;
        case SIGILL:
            msg = "SIGILL: Illegal instruction";
            break;
        case SIGABRT:
            msg = "SIGABRT: Abnormal termination";
            break;
        case SIGFPE:
            msg = "SIGFPE: Arithmetical error";
            break;
        default:
            return;
    }
#    if !defined(_WIN32)
    write_safe_report(msg);
    if (test_mode || requested_driver_window()) {
        // Nobody is at the keyboard, and the full report below allocates: when the abort came
        // from inside the allocator (heap corruption) the first allocation deadlocks and the
        // process hangs instead of dying. The report above is all a test run needs; re-raise
        // so the exit status carries the signal (the default action is restored above).
        raise(sig);
        return;
    }
#    endif
    log_crash("Signal", msg);
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wunknown-pragmas"
#    pragma GCC diagnostic ignored "-Wold-style-cast"
#    pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
    std::signal(SIGABRT, SIG_DFL);
#    pragma GCC diagnostic pop
    abort();
}
} // extern "C"

#    if defined(_WIN32)
static LONG WINAPI windows_exception_filter(EXCEPTION_POINTERS* exception_info) {
    if (g_crash_logged.test_and_set()) { return EXCEPTION_CONTINUE_SEARCH; }
    g_exception_info = exception_info;
    set_crash_exception_context(exception_info ? exception_info->ContextRecord : nullptr);
    const char* msg = "Unknown exception";
    if (exception_info && exception_info->ExceptionRecord) {
        switch (exception_info->ExceptionRecord->ExceptionCode) {
            case EXCEPTION_ACCESS_VIOLATION:
                msg = "EXCEPTION_ACCESS_VIOLATION: Access violation";
                break;
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                msg = "EXCEPTION_ILLEGAL_INSTRUCTION: Illegal instruction";
                break;
            case EXCEPTION_STACK_OVERFLOW:
                msg = "EXCEPTION_STACK_OVERFLOW: Stack overflow";
                break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                msg = "EXCEPTION_INT_DIVIDE_BY_ZERO: Integer divide by zero";
                break;
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                msg = "EXCEPTION_FLT_DIVIDE_BY_ZERO: Float divide by zero";
                break;
            default:
                break;
        }
    }
    log_crash("Signal", msg);
    return EXCEPTION_CONTINUE_SEARCH;
}
#    endif

[[noreturn]] static void crash_terminate_handler() {
    // TODO: thread-safety?
    const char* type;
    const char* msg;
    try {
        auto&& ex = std::current_exception();
        if (ex) {
            std::rethrow_exception(ex);
        } else {
            type = msg = "Unexpected termination";
        }
    } catch (const std::exception& e) {
        type = typeid(e).name();
        msg = e.what();
        // call here to avoid `msg = e.what()` going out of scope
        log_crash(type, msg);
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wunknown-pragmas"
#    pragma GCC diagnostic ignored "-Wold-style-cast"
#    pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
        std::signal(SIGABRT, SIG_DFL);
#    pragma GCC diagnostic pop
        abort();
    } catch (...) {
        type = "Unknown exception";
        msg = "Not derived from std::exception";
    }
    log_crash(type, msg);
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wunknown-pragmas"
#    pragma GCC diagnostic ignored "-Wold-style-cast"
#    pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
    std::signal(SIGABRT, SIG_DFL);
#    pragma GCC diagnostic pop
    abort();
}

void init_crash_handlers() {
#    if defined(_WIN32)
    // On Windows, SIGSEGV/SIGILL/SIGFPE are translated from SEH hardware exceptions
    // by the CRT via a Vectored Exception Handler (VEH). Registering C signal handlers
    // for these causes that VEH to intercept them before SetUnhandledExceptionFilter
    // fires, which loses the EXCEPTION_POINTERS context needed for a useful minidump
    // and accurate stack trace. Only SIGABRT is registered here since abort() raises
    // it explicitly rather than through SEH.
    std::signal(SIGABRT, signal_handler);
    SetUnhandledExceptionFilter(windows_exception_filter);
#    else
    // backtrace() loads libgcc lazily and allocates on its first call; do that now, not in a
    // handler that may be running inside the allocator.
    void* primer[1];
    backtrace(primer, 1);
    for (auto sig : {SIGSEGV, SIGILL, SIGABRT, SIGFPE}) { std::signal(sig, signal_handler); }
#    endif
    std::set_terminate(crash_terminate_handler);
}

#else // !BACKTRACE

void init_crash_handlers() {}

#endif
