/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#define WIN32_LEAN_AND_MEAN
#pragma warning( push )
#pragma warning( disable : 4255 4668 )
#include <windows.h>
#pragma warning( pop )

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>

using std::string;

class Handle {
    public:
        Handle() : handle_(INVALID_HANDLE_VALUE) { }

        Handle(const Handle& h) : handle_(h.handle_) { }

        ~Handle() {
            close();
        }

        Handle& operator=(HANDLE handle) {
            close();
            handle_ = handle;
            return *this;
        }

        bool duplicate(const Handle& h) {
            const HANDLE process = GetCurrentProcess();
            HANDLE new_handle    = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(process, h.handle_,
                                 process, &new_handle,
                                 0, TRUE, DUPLICATE_SAME_ACCESS)) {
                handle_ = INVALID_HANDLE_VALUE;
                printf("Error: Failed to duplicate handle\n");
                return false;
            }
            close();
            handle_ = new_handle;
            return true;
        }

        void close() {
            if (handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
                handle_ = INVALID_HANDLE_VALUE;
            }
        }

        operator HANDLE() const {
            return handle_;
        }

    private:
        HANDLE handle_;
};

bool create_pipe(Handle* read, Handle* write)
{
    SECURITY_ATTRIBUTES sa  = { };
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    HANDLE read_handle  = INVALID_HANDLE_VALUE;
    HANDLE write_handle = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&read_handle, &write_handle, &sa, 0)) {
        printf("Error: Failed to create pipe\n");
        return false;
    }

    *read  = read_handle;
    *write = write_handle;

    return true;
}

class DepGenerator {
    public:
        DepGenerator(HANDLE        console,
                     HANDLE        dep_file,
                     const string& dep_file_name,
                     const string& obj_file_name)
            : console_(      console),
              dep_file_(     dep_file),
              dep_file_name_(dep_file_name),
              obj_file_name_(obj_file_name),
              cygwin_(       false),
              line_num_(     0)
        {
            char buf[8] = { };
            GetEnvironmentVariable("OSTYPE", buf, sizeof(buf));
            if (0 == strncmp(buf, "cygwin", sizeof(buf)))
                cygwin_ = true;
        }

        bool process_line(string& line)
        {
            static const char note_including[] = "Note: including file:";

            if (line.size() > sizeof(note_including) &&
                0 == memcmp(&line[0], note_including, sizeof(note_including)-1)) {

                string rule = obj_file_name_ + " : ";

                size_t i = sizeof(note_including) - 1;
                while (i < line.size() && line[i] == ' ')
                    i++;

                if (i + 2 < line.size() && line[i+1] == ':') {
                    if (cygwin_)
                        rule += "/cygdrive/";
                    else
                        rule += '/';
                    rule += line[i];
                    i += 2;
                }

                for ( ; i < line.size(); i++) {
                    const char c = line[i];
                    if (c == '\r' || c == '\n')
                        break;
                    switch (c) {
                        case '\\': rule += '/';   break;
                        case ' ':  rule += "\\ "; break;
                        case ':':  rule += "\\:"; break;
                        default:   rule += c;     break;
                    }
                }

                rule += '\n';

                DWORD num_written = 0;
                if (!WriteFile(dep_file_,
                               &rule[0],
                               (DWORD)rule.size(),
                               &num_written,
                               NULL) || num_written != rule.size()) {
                    printf("Error: Failed to write dep file \"%s\"\n", dep_file_name_.c_str());
                    return false;
                }
            }
            else {
                // Skip first line which shows file name
                if (++line_num_ == 1)
                    return true;

                DWORD num_written = 0;
                if (!WriteFile(console_,
                               &line[0],
                               (DWORD)line.size(),
                               &num_written,
                               NULL) || num_written != line.size()) {
                    printf("Error: Failed to write to console\n");
                    return false;
                }
            }

            return true;
        }

    private:
        DepGenerator& operator=(const DepGenerator&);

        HANDLE        console_;
        HANDLE        dep_file_;
        const string& dep_file_name_;
        const string& obj_file_name_;
        bool          cygwin_;
        int           line_num_;
};

class FileDeletor {
    public:
        FileDeletor(HANDLE handle, const string& file_name)
            : handle_(handle),
              file_name_(file_name)
        { }
        ~FileDeletor() {
            if (handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
                DeleteFile(file_name_.c_str());
            }
        }
        void cancel() {
            handle_ = INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE handle_;
        string file_name_;
};

int main(int argc, char *argv[])
{
    string cmd_line = "cl -showIncludes";
    string obj_file_name;

    for (int i = 1; i < argc; i++) {
        const string arg = argv[i];

        if (arg.size() > 3 && (arg[0] == '-' || arg[0] == '/') &&
                arg[1] == 'F' && arg[2] == 'o')
            obj_file_name = arg.substr(3);

        cmd_line += " " + arg;
    }
    cmd_line += '\0';

    string dep_file_name = obj_file_name;
    {
        bool dep_file_name_ok = false;
        if (dep_file_name.size() > 4)
            if (dep_file_name.substr(dep_file_name.size() - 4) == ".obj") {
                dep_file_name    = dep_file_name.substr(0, dep_file_name.size() - 4) + ".d";
                dep_file_name_ok = true;
            }
        if (!dep_file_name_ok) {
            if (dep_file_name.empty()) {
                printf("Error: Missing -Fo argument!\n");
                return 1;
            }
            else {
                printf("Error: Output file does not have .obj extension - \"%s\"\n", dep_file_name.c_str());
                return 1;
            }
        }
    }

    Handle std_out;
    Handle std_err;
    Handle output;

    if (!create_pipe(&output, &std_out))
        return (int)GetLastError();
    if (!std_err.duplicate(std_out))
        return (int)GetLastError();

    if (!SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0)) {
        printf("Error: Failed to set up pipe\n");
        return 1;
    }

    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

    HANDLE dep_file = CreateFile(dep_file_name.c_str(),
                                 GENERIC_WRITE,
                                 0,
                                 NULL,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,
                                 0);
    if (dep_file == INVALID_HANDLE_VALUE) {
        printf("Error: Failed to create \"%s\"\n", dep_file_name.c_str());
        return (int)GetLastError();
    }

    FileDeletor delete_file(dep_file, dep_file_name);

    STARTUPINFO startup_info = { };
    startup_info.cb          = sizeof(startup_info);
    startup_info.dwFlags     = STARTF_USESTDHANDLES;
    startup_info.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput  = std_out;
    startup_info.hStdError   = std_err;

    PROCESS_INFORMATION process_info = { };

    DWORD exit_code = 1;

    if (CreateProcess(NULL,
                      &cmd_line[0],
                      NULL,
                      NULL,
                      TRUE,
                      0,
                      NULL,
                      NULL,
                      &startup_info,
                      &process_info)) {

        std_out.close();
        std_err.close();

        CloseHandle(process_info.hThread);

        char   buf[1024];
        string line;

        DepGenerator gen(console, dep_file, dep_file_name, obj_file_name);

        for (;;) {
            DWORD num_read = 0;
            if (!ReadFile(output, buf, sizeof(buf), &num_read, NULL) || num_read == 0) {
                if (GetLastError() == ERROR_BROKEN_PIPE)
                    break;
                printf("Error: Failed to read compiler output\n");
                return (int)GetLastError();
            }

            DWORD pos = 0;
            DWORD i   = 0;
            for ( ; i < num_read; i++) {
                if (buf[i] == '\n') {
                    line.append(&buf[pos], i+1-pos);
                    pos = i + 1;

                    if (!gen.process_line(line))
                        return (int)GetLastError();
                    line.clear();
                }
                else {
                    if (i == num_read-1)
                        line.append(&buf[pos], i+1-pos);
                }
            }
        }

        if (!line.empty())
            if (!gen.process_line(line))
                return (int)GetLastError();

        GetExitCodeProcess(process_info.hProcess, &exit_code);

        CloseHandle(process_info.hProcess);
    }

    if (exit_code == 0) {
        delete_file.cancel();
        CloseHandle(dep_file);
    }

    return (int)exit_code;
}
