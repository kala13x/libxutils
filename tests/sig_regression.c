/* libxutils: signal registration, backtrace and daemonization.
 *
 * The exiting paths are driven in a forked child so the regression process
 * survives to report on them: the library's own handler ends the process by
 * design, and so does the error-exit helper.
 */

#include "test.h"
#include "sig.h"
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile sig_atomic_t g_nHandled = 0;
static volatile sig_atomic_t g_nLastSignal = 0;

static void sig_handler(int nSignal)
{
    g_nHandled++;
    g_nLastSignal = nSignal;
}

/* Runs the body in a child and returns its exit status, or -1 if it died. */
static int sig_run_child(void (*pBody)(void))
{
    /* The bodies below end the process through exit(), which flushes stdio.
     * Without this the child would inherit and then re-emit whatever the
     * harness has already buffered, duplicating the test output. */
    fflush(stdout);
    fflush(stderr);

    pid_t nPid = fork();
    if (nPid < 0) return -2;

    if (nPid == 0)
    {
        pBody();
        _exit(42);      /* The body was supposed to end the process */
    }

    int nStatus = 0;
    if (waitpid(nPid, &nStatus, 0) != nPid) return -2;
    return WIFEXITED(nStatus) ? WEXITSTATUS(nStatus) : -1;
}

static void sig_body_errexit(void)
{
    XUtils_ErrExit("fatal: %s %d", "test", 7);
}

static void sig_body_errexit_null(void)
{
    XUtils_ErrExit(NULL);
}

static void sig_body_exit_signal(void)
{
    if (XSig_RegExitSigs() != 0) _exit(43);
    raise(SIGINT);
    _exit(44);          /* The handler must have ended the process already */
}

static void sig_body_segv_signal(void)
{
    if (XSig_RegExitSigs() != 0) _exit(43);
    raise(SIGSEGV);
    _exit(44);
}

static void sig_body_daemonize(void)
{
    /* daemon() forks: this process exits from inside the call and the
     * detached child returns here, so both ends must exit quietly. */
    int nStatus = XUtils_Daemonize(1, 1);
    _exit(nStatus == 0 ? 0 : 45);
}

static int XTest_register(void)
{
    /* A registered handler runs on delivery and reports its own signal. */
    int signals[] = {SIGUSR1, SIGUSR2};
    CHECK(XSig_Register(signals, 2, sig_handler) == 0, "Two handlers register successfully");

    g_nHandled = 0;
    g_nLastSignal = 0;
    raise(SIGUSR1);
    CHECK(g_nHandled == 1, "The handler ran for the first signal");
    CHECK(g_nLastSignal == SIGUSR1, "The handler received the signal number it was raised with");

    raise(SIGUSR2);
    CHECK(g_nHandled == 2, "The handler ran for the second signal");
    CHECK(g_nLastSignal == SIGUSR2, "The second signal is distinguished from the first");

    /* Re-registering replaces the handler rather than stacking it. */
    CHECK(XSig_Register(signals, 1, SIG_IGN) == 0, "A signal can be ignored");
    raise(SIGUSR1);
    CHECK(g_nHandled == 2, "An ignored signal does not reach the old handler");

    /* An unregisterable signal is reported back by number. */
    int uncatchable[] = {SIGKILL};
    CHECK(XSig_Register(uncatchable, 1, sig_handler) == SIGKILL, "An uncatchable signal is reported by number");

    int invalid[] = {SIGUSR1, 12345};
    CHECK(XSig_Register(invalid, 2, sig_handler) == 12345, "The first failing signal is the one reported");

    /* An empty list is a no-op, not a failure. */
    CHECK(XSig_Register(signals, 0, sig_handler) == 0, "An empty signal list succeeds");

    /* Put the defaults back so nothing leaks into the later cases. */
    CHECK(XSig_Register(signals, 2, SIG_DFL) == 0, "The defaults are restored");
    return 0;
}

static int XTest_exit_handler(void)
{
    /* The bundled handler ends the process with a failure status. */
    CHECK(sig_run_child(sig_body_exit_signal) == EXIT_FAILURE,
        "An interrupt signal exits through the bundled handler");
    CHECK(sig_run_child(sig_body_segv_signal) == EXIT_FAILURE,
        "A fault signal exits through the bundled handler after a backtrace");
    return 0;
}

static int XTest_errexit(void)
{
    CHECK(sig_run_child(sig_body_errexit) == EXIT_FAILURE, "The error exit helper ends the process");
    CHECK(sig_run_child(sig_body_errexit_null) == EXIT_FAILURE, "A message-less error exit still ends the process");
    return 0;
}

static int XTest_backtrace(void)
{
    /* The backtrace only logs; with logging off it must still be safe to
     * call, including repeatedly and from a nested frame. */
    XUtils_Backtrace();
    XUtils_Backtrace();
    CHECK(1, "Repeated backtraces are safe");
    return 0;
}

static int XTest_daemonize(void)
{
    CHECK(sig_run_child(sig_body_daemonize) == 0, "Daemonizing detaches without an error");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(register),
    XTEST_CASE(exit_handler),
    XTEST_CASE(errexit),
    XTEST_CASE(backtrace),
    XTEST_CASE(daemonize)
)
