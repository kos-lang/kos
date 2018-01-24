/*
 * Copyright (c) 2014-2018 Chris Dragan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define WIN32_LEAN_AND_MEAN
#pragma warning( push )
#pragma warning( disable : 4255 4668 )
#include <windows.h>
#pragma warning( pop )

#include <stdio.h>
#include <string.h>
#include <string>

using std::string;

class Handle {
    public:
        Handle() : _handle(INVALID_HANDLE_VALUE) { }

        Handle(const Handle& h) : _handle(INVALID_HANDLE_VALUE) {
            *this = h;
        }

        ~Handle() {
            close();
        }

        Handle& operator=(HANDLE handle) {
            close();
            _handle = handle;
            return *this;
        }

        bool duplicate(const Handle& h) {
            const HANDLE process = GetCurrentProcess();
            HANDLE new_handle    = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(process, h._handle,
                                 process, &new_handle,
                                 0, TRUE, DUPLICATE_SAME_ACCESS)) {
                _handle = INVALID_HANDLE_VALUE;
                printf("Error: Failed to duplicate handle\n");
                return false;
            }
            close();
            _handle = new_handle;
            return true;
        }

        void close() {
            if (_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(_handle);
                _handle = INVALID_HANDLE_VALUE;
            }
        }

        operator HANDLE() const {
            return _handle;
        }

    private:
        HANDLE _handle;
};

bool create_pipe(Handle* read, Handle* write)
{
    SECURITY_ATTRIBUTES sa  = { };
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = 0;
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
            : _console(      console),
              _dep_file(     dep_file),
              _dep_file_name(dep_file_name),
              _obj_file_name(obj_file_name),
              _cygwin(       false),
              _line_num(     0)
        {
            char buf[8] = { };
            GetEnvironmentVariable("OSTYPE", buf, sizeof(buf));
            if (0 == strncmp(buf, "cygwin", sizeof(buf)))
                _cygwin = true;
        }

        bool process_line(string& line)
        {
            static const char note_including[] = "Note: including file:";

            if (line.size() > sizeof(note_including) &&
                0 == memcmp(&line[0], note_including, sizeof(note_including)-1)) {

                string rule = _obj_file_name + " : ";

                size_t i = sizeof(note_including) - 1;
                while (i < line.size() && line[i] == ' ')
                    i++;

                if (i + 2 < line.size() && line[i+1] == ':') {
                    if (_cygwin)
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
                if (!WriteFile(_dep_file,
                               &rule[0],
                               (DWORD)rule.size(),
                               &num_written,
                               0) || num_written != rule.size()) {
                    printf("Error: Failed to write dep file \"%s\"\n", _dep_file_name.c_str());
                    return false;
                }
            }
            else {
                // Skip first line which shows file name
                if (++_line_num == 1)
                    return true;

                DWORD num_written = 0;
                if (!WriteFile(_console,
                               &line[0],
                               (DWORD)line.size(),
                               &num_written,
                               0) || num_written != line.size()) {
                    printf("Error: Failed to write to console\n");
                    return false;
                }
            }

            return true;
        }

    private:
        DepGenerator& operator=(const DepGenerator&);

        HANDLE        _console;
        HANDLE        _dep_file;
        const string& _dep_file_name;
        const string& _obj_file_name;
        bool          _cygwin;
        int           _line_num;
};

class FileDeletor {
    public:
        FileDeletor(HANDLE handle, const string& file_name)
            : _handle(handle),
              _file_name(file_name)
        { }
        ~FileDeletor() {
            if (_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(_handle);
                DeleteFile(_file_name.c_str());
            }
        }
        void cancel() {
            _handle = INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE _handle;
        string _file_name;
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
                                 0,
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

    if (CreateProcess(0,
                      &cmd_line[0],
                      0,
                      0,
                      TRUE,
                      0,
                      0,
                      0,
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
            if (!ReadFile(output, buf, sizeof(buf), &num_read, 0) || num_read == 0) {
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
