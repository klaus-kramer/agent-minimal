// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "ExecuteCommandTool.h"
#include "core/JsonHelper.h"

#include <sstream>
#include <cstdio>
#include <memory>
#include <cstring>
#include <algorithm>
#include <cctype>

static constexpr size_t MAX_CMD_OUTPUT = 262144;
static constexpr int MAX_CMD_SECONDS = 60;

#ifdef _WIN32
#include <windows.h>

static ToolResult runCommand(const std::string &command)
{
    std::string cmdLine = "cmd.exe /c " + command + " 2>&1";

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return {false, "Failed to create pipe"};
    }

    if (!SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return {false, "Failed to set pipe handle"};
    }

    PROCESS_INFORMATION pi = {};
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdError = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::string mutableCmd = cmdLine;
    if (!CreateProcessA(NULL, &mutableCmd[0], NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return {false, "Failed to create process"};
    }

    CloseHandle(hWrite);

    std::ostringstream os;
    char buf[4096];
    DWORD bytesRead;
    size_t total = 0;

    DWORD startTime = GetTickCount();
    bool timedOut = false;

    while (true) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 200);
        if (waitResult == WAIT_TIMEOUT) {
            if (GetTickCount() - startTime > static_cast<DWORD>(MAX_CMD_SECONDS) * 1000) {
                timedOut = true;
                TerminateProcess(pi.hProcess, 1);
                break;
            }

            while (PeekNamedPipe(hRead, NULL, 0, NULL, &bytesRead, NULL) && bytesRead > 0) {
                if (total + bytesRead > MAX_CMD_OUTPUT) {
                    DWORD toRead = static_cast<DWORD>(MAX_CMD_OUTPUT - total);
                    if (toRead > 0) {
                        ReadFile(hRead, buf, toRead, &bytesRead, NULL);
                        os.write(buf, bytesRead);
                        total += bytesRead;
                    }

                    char drain[4096];
                    while (ReadFile(hRead, drain, sizeof(drain), &bytesRead, NULL) && bytesRead > 0);
                    break;
                }
                if (!ReadFile(hRead, buf, sizeof(buf), &bytesRead, NULL) || bytesRead == 0)
                    break;
                os.write(buf, bytesRead);
                total += bytesRead;
            }
        } else if (waitResult == WAIT_OBJECT_0) {

            while (true) {
                if (!PeekNamedPipe(hRead, NULL, 0, NULL, &bytesRead, NULL) || bytesRead == 0)
                    break;
                if (total + bytesRead > MAX_CMD_OUTPUT) {
                    DWORD toRead = static_cast<DWORD>(MAX_CMD_OUTPUT - total);
                    if (toRead > 0) {
                        ReadFile(hRead, buf, toRead, &bytesRead, NULL);
                        os.write(buf, bytesRead);
                        total += bytesRead;
                    }
                    char drain[4096];
                    while (ReadFile(hRead, drain, sizeof(drain), &bytesRead, NULL) && bytesRead > 0);
                    break;
                }
                if (!ReadFile(hRead, buf, sizeof(buf), &bytesRead, NULL) || bytesRead == 0)
                    break;
                os.write(buf, bytesRead);
                total += bytesRead;
            }
            break;
        } else {
            break;
        }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::string output = os.str();
    if (timedOut)
        output += "\n(Command timed out after " + std::to_string(MAX_CMD_SECONDS) + " seconds)";
    if (total > MAX_CMD_OUTPUT)
        output += "\n(Output truncated at " + std::to_string(MAX_CMD_OUTPUT) + " bytes)";

    return {exitCode == 0, output};
}

#else

#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

static ToolResult runCommand(const std::string &command)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return {false, "Failed to create pipe"};
    }

    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {false, "Failed to fork"};
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO)
            close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);

    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    std::ostringstream os;
    char buf[4096];
    ssize_t n;
    size_t total = 0;
    bool timedOut = false;

    struct timeval start;
    gettimeofday(&start, NULL);

    while (true) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
                if (total < MAX_CMD_OUTPUT) {
                    size_t toWrite = std::min(static_cast<size_t>(n), MAX_CMD_OUTPUT - total);
                    os.write(buf, static_cast<std::streamsize>(toWrite));
                    total += toWrite;
                }
            close(pipefd[0]);
            bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            return {success, os.str()};
        }

        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
            if (total < MAX_CMD_OUTPUT) {
                size_t toWrite = std::min(static_cast<size_t>(n), MAX_CMD_OUTPUT - total);
                os.write(buf, static_cast<std::streamsize>(toWrite));
                total += toWrite;
            }

        struct timeval now;
        gettimeofday(&now, NULL);
        double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1e6;
        if (elapsed > MAX_CMD_SECONDS) {
            timedOut = true;
            kill(pid, SIGTERM);
            usleep(100000);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            break;
        }

        usleep(50000);
    }

    std::string output = os.str();
    if (timedOut)
        output += "\n(Command timed out after " + std::to_string(MAX_CMD_SECONDS) + " seconds)";
    if (total >= MAX_CMD_OUTPUT)
        output += "\n(Output truncated at " + std::to_string(MAX_CMD_OUTPUT) + " bytes)";

    return {false, output};
}

#endif

static bool isCommandDenied(const std::string &cmd)
{
    std::string lower = cmd;
    for (auto &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::string first;
    for (char c : lower) {
        if (c == ' ' || c == '\t' || c == '|' || c == ';' || c == '&' || c == '>' || c == '<')
            break;
        first += c;
    }

    static const char *deniedCommands[] = {
        "format",  "diskpart", "shutdown", "reboot",   "regedit",
        "msiexec", "takeown",  "del",      "erase",    "rmdir",
        "rd",      "rm",       "rmtree",   "dd",       "mkfs",
        "mke2fs",  "fdisk",    " parted",  " wipefs",
    };
    for (auto *d : deniedCommands) {
        if (first == d) return true;

        if (first.size() > std::strlen(d)) {
            std::string tail = first.substr(first.size() - std::strlen(d));
            if (tail == d) {
                char sep = first[first.size() - std::strlen(d) - 1];
                if (sep == '/' || sep == '\\') return true;
            }
        }
    }

    static const char *deniedPatterns[] = {
        "rm -rf /",       "rm -rf /*",      "rm -r /",       "rm -fr /",
        "rm -rf /",       "rm -rf --no-preserve-root",
        "> \\\\.\\",      "> \\\\.\\physicaldrive",
        "dd if=",
        "> /dev/sda",     "> /dev/nvme",    "> /dev/mmcblk",
        "chmod 777 /",    "chown -r /",     "chown -r /*",
        "icacls ",        "cacls ",         "attrib ",
        "taskkill /f",    "kill -9 ",
        "reg delete",     "reg add",        "reg import",
        "bcdedit",        "bootsect",       "bootrec",
        "sfc /",          "dism /",         "chkdsk /f",
    };
    for (auto *p : deniedPatterns) {
        if (lower.find(p) != std::string::npos) return true;
    }

    return false;
}

static ToolResult executeCommandTool(const ToolCall &call)
{
    std::string command = json_helper::extractToolArg(call.rawArguments, "command");
    if (command.empty()) return {false, "Missing parameter: command"};

    if (isCommandDenied(command)) {
        return {false, "Command denied: '" + command + "' is not allowed for security reasons."};
    }

    return runCommand(command);
}

void registerExecuteCommandTool(ToolRegistry &registry)
{
    ToolDefinition def;
    def.name = "execute_command";
    def.description = "Execute a shell command and return its output. The command runs via cmd.exe on Windows or /bin/sh on other platforms. Use with caution.";
    def.parameters = {
        {"command", "The shell command to execute", "string", true}
    };
    def.requiresConfirmation = true;
    registry.registerTool(def, executeCommandTool);
}
