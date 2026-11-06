#include "../../Log.hpp"
#include "../../hyprctlCompat.hpp"
#include "../shared.hpp"
#include "build.hpp"
#include "tests.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <exception>
#include <format>
#include <memory>
#include <string>
#include <sys/poll.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/os/FileDescriptor.hpp>
#include <hyprutils/os/Process.hpp>

using namespace Hyprutils::Memory;
using namespace Hyprutils::OS;

#define SP CSharedPointer

static constexpr int  CLIENT_COUNT = 6;
static constexpr auto OUTPUT_NAME  = "WL-OUTPUT-RACE";

namespace {

    class CClient {
      public:
        CClient();
        ~CClient();

        // Tests::processAlive can't tell: an exited client stays a zombie until it is reaped, and
        // kill(pid, 0) succeeds on a zombie. The client prints nothing after "started" unless it was
        // disconnected, and its exit closes the pipe, so any activity here means it is gone.
        bool connected();

      private:
        SP<CProcess>    m_proc;
        CFileDescriptor m_readFd;
    };
}

CClient::CClient() {
    m_proc = makeShared<CProcess>(std::format("{}/wl-output-race", binaryDir), std::vector<std::string>{});
    m_proc->addEnv("WAYLAND_DISPLAY", WLDISPLAY);

    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        NLog::log("{}Unable to open pipe to wl-output-race", Colors::RED);
        throw std::exception();
    }

    m_readFd = CFileDescriptor(pipeFds[0]);
    m_proc->setStdoutFD(pipeFds[1]);
    m_proc->runAsync();
    close(pipeFds[1]);

    pollfd fds = {.fd = m_readFd.get(), .events = POLLIN};
    if (poll(&fds, 1, 5000) != 1 || !(fds.revents & POLLIN)) {
        NLog::log("{}wl-output-race client failed poll", Colors::RED);
        throw std::exception();
    }

    std::array<char, 64> buffer = {};
    if (read(m_readFd.get(), buffer.data(), buffer.size() - 1) <= 0 || !std::string{buffer.data()}.contains("started")) {
        NLog::log("{}wl-output-race client failed to start", Colors::RED);
        throw std::exception();
    }
}

CClient::~CClient() {
    kill(m_proc->pid(), SIGKILL);
}

bool CClient::connected() {
    pollfd fds = {.fd = m_readFd.get(), .events = POLLIN};
    return poll(&fds, 1, 0) == 0;
}

// Runs `commands` as one IPC batch, so everything lands in a single event loop iteration, before any
// client can react, and returns how many clients are still connected afterwards.
static int clientsSurviving(const std::string& commands) {
    std::vector<std::unique_ptr<CClient>> clients;
    for (int i = 0; i < CLIENT_COUNT; ++i) {
        clients.emplace_back(std::make_unique<CClient>());
    }

    const auto REPLY = getFromSocket(std::format("[[BATCH]]{}", commands));

    // without this the sequence silently does nothing, and every client survives for the wrong reason
    if (REPLY.contains("not found")) {
        NLog::log("{}The output never existed: {}", Colors::RED, REPLY);
        throw std::exception();
    }

    // clients need a moment to receive the globals, bind them and get their errors back
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int surviving = 0;
    for (const auto& client : clients) {
        surviving += client->connected();
    }

    // may fail: not every sequence leaves the output behind
    getFromSocket(std::format("/output remove {}", OUTPUT_NAME));

    return surviving;
}

// A client that saw a wl_output global and is still binding it must not be killed because the output
// is already gone: the global has to outlive the removal, not just the resources already bound to it.
//
// This has to happen inside one batch. Given any time, Xwayland binds the output and never releases it,
// which keeps the global alive and hides the bug.
TEST_CASE(wlOutputRemovedWhileBinding) {
    const auto CREATE = std::format("output create headless {}", OUTPUT_NAME);
    const auto REMOVE = std::format("output remove {}", OUTPUT_NAME);

    // test.lua disables outputs it has no rule for, and a disabled output can't be removed by name
    OK(getFromSocket(std::format("/eval hl.monitor({{ output = '{}', mode = '1920x1080@60', position = 'auto', scale = 1 }})", OUTPUT_NAME)));

    try {
        // the first client to bind and release must not take the global away from the others
        EXPECT(clientsSurviving(std::format("{};{}", CREATE, REMOVE)), CLIENT_COUNT);

        // re-adding the name must not destroy the old global while it has binds in flight
        EXPECT(clientsSurviving(std::format("{};{};{}", CREATE, REMOVE, CREATE)), CLIENT_COUNT);
    } catch (...) { FAIL_TEST("Couldn't start the clients"); }
}
