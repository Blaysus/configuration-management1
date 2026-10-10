#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>
#include <ctime>
#include <iomanip>
#include <sys/utsname.h>
#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace std;

struct Entry {
    bool directory;
    string data;
};

const string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string encode64(const string& text) {
    string result;
    unsigned int buffer = 0;
    int bits = 0;
    for (unsigned char c : text) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            result += alphabet[(buffer >> bits) & 63];
        }
    }
    if (bits != 0) result += alphabet[(buffer << (6 - bits)) & 63];
    while (result.size() % 4 != 0) result += '=';
    return result;
}

string decode64(const string& text) {
    string result;
    unsigned int buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        buffer = (buffer << 6) | static_cast<unsigned int>(alphabet.find(c));
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result += static_cast<char>((buffer >> bits) & 255);
        }
    }
    return result;
}

string fullPath(const string& path, const string& current = "/") {
    string source = (!path.empty() && path[0] == '/') ? path : current + "/" + path;
    vector<string> parts;
    string part;
    for (char c : source + "/") {
        if (c != '/') {
            part += c;
        } else {
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            part.clear();
        }
    }
    string result;
    for (const auto& item : parts) result += "/" + item;
    return result.empty() ? "/" : result;
}

void addEntry(map<string, Entry>& files, const string& path,
              bool directory, const string& data) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        string parent = path.substr(0, i);
        if (files.count(parent) && !files[parent].directory) {
            throw runtime_error("файл используется как папка: " + parent);
        }
        files[parent] = {true, ""};
    }
    if (files.count(path) && (!directory || !files[path].directory)) {
        throw runtime_error("повторяющийся путь в ZIP: " + path);
    }
    files[path] = {directory, directory ? "" : encode64(data)};
}

uint32_t zipNumber(const string& bytes, size_t pos, size_t count) {
    if (pos > bytes.size() || count > bytes.size() - pos) {
        throw runtime_error("повреждённый ZIP: данные обрезаны");
    }
    uint32_t result = 0;
    for (size_t i = 0; i < count; ++i) {
        result |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[pos + i])) << (8 * i);
    }
    return result;
}

string unzipData(const string& bytes, size_t pos, size_t packed,
                      size_t size, unsigned int method) {
    if (pos > bytes.size() || packed > bytes.size() - pos) {
        throw runtime_error("повреждённые данные файла в ZIP");
    }
    if (method == 0) {
        if (packed != size) throw runtime_error("неверный размер файла в ZIP");
        return bytes.substr(pos, size);
    }
    if (method != 8) throw runtime_error("поддерживаются ZIP Store и Deflate");
    string result(size + 1, '\0');
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data() + pos));
    stream.avail_in = static_cast<uInt>(packed);
    stream.next_out = reinterpret_cast<Bytef*>(&result[0]);
    stream.avail_out = static_cast<uInt>(result.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        throw runtime_error("не удалось запустить распаковку в память");
    }
    int status = inflate(&stream, Z_FINISH);
    bool valid = status == Z_STREAM_END && stream.total_out == size && stream.total_in == packed;
    inflateEnd(&stream);
    if (!valid) throw runtime_error("ошибка распаковки ZIP");
    result.resize(size);
    return result;
}

map<string, Entry> loadZip(const string& filename) {
    ifstream input(filename, ios::binary | ios::ate);
    if (!input) throw runtime_error("не удалось открыть VFS: " + filename);
    const size_t limit = 64 * 1024 * 1024;
    if (input.tellg() < 0 || input.tellg() > static_cast<streamoff>(limit)) {
        throw runtime_error("ZIP слишком большой: предел 64 МиБ");
    }
    input.seekg(0);
    string bytes((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
    if (bytes.size() < 22) throw runtime_error("файл не является ZIP-архивом");
    size_t end = bytes.size() - 22;
    size_t first = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
    while (true) {
        if (zipNumber(bytes, end, 4) == 0x06054b50 &&
            end + 22 + zipNumber(bytes, end + 20, 2) == bytes.size()) break;
        if (end == first) throw runtime_error("не найден конец ZIP-архива");
        --end;
    }
    unsigned int count = zipNumber(bytes, end + 10, 2);
    size_t pos = zipNumber(bytes, end + 16, 4);
    size_t centralSize = zipNumber(bytes, end + 12, 4);
    if (zipNumber(bytes, end + 4, 2) || zipNumber(bytes, end + 6, 2) ||
        zipNumber(bytes, end + 8, 2) != count || count == 65535 || pos > end || centralSize != end - pos) {
        throw runtime_error("многотомный ZIP, ZIP64 или повреждённый архив не поддерживается");
    }
    map<string, Entry> files = {{"/", {true, ""}}};
    size_t total = 0;
    for (unsigned int i = 0; i < count; ++i) {
        if (zipNumber(bytes, pos, 4) != 0x02014b50) throw runtime_error("ошибка каталога ZIP");
        unsigned int flags = zipNumber(bytes, pos + 8, 2);
        unsigned int method = zipNumber(bytes, pos + 10, 2);
        size_t packed = zipNumber(bytes, pos + 20, 4);
        size_t size = zipNumber(bytes, pos + 24, 4);
        size_t nameSize = zipNumber(bytes, pos + 28, 2);
        size_t next = pos + 46 + nameSize + zipNumber(bytes, pos + 30, 2) + zipNumber(bytes, pos + 32, 2);
        size_t local = zipNumber(bytes, pos + 42, 4);
        unsigned int mode = zipNumber(bytes, pos + 38, 4) >> 16;
        if (flags & 1) throw runtime_error("ZIP с паролем не поддерживается");
        if ((mode & 0170000) == 0120000) throw runtime_error("символические ссылки не поддерживаются");
        if (next > end || nameSize == 0 || size > limit - total) {
            throw runtime_error("повреждённый ZIP или размер VFS больше 64 МиБ");
        }
        total += size;
        string name = bytes.substr(pos + 46, nameSize);
        if (name[0] == '/' || name.find('\0') != string::npos ||
            name.find('\\') != string::npos || ("/" + name + "/").find("/../") != string::npos) {
            throw runtime_error("недопустимый путь в ZIP");
        }
        if (zipNumber(bytes, local, 4) != 0x04034b50 ||
            zipNumber(bytes, local + 8, 2) != method || zipNumber(bytes, local + 6, 2) != flags) {
            throw runtime_error("ошибка заголовка ZIP");
        }
        size_t localName = zipNumber(bytes, local + 26, 2);
        size_t dataPos = local + 30 + localName + zipNumber(bytes, local + 28, 2);
        if (bytes.substr(local + 30, localName) != name || dataPos > pos || packed > pos - dataPos) {
            throw runtime_error("неверное расположение данных ZIP");
        }
        string data = unzipData(bytes, dataPos, packed, size, method);
        auto crc = crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
        if (crc != zipNumber(bytes, pos + 16, 4)) throw runtime_error("не совпала контрольная сумма ZIP");
        addEntry(files, fullPath(name), name.back() == '/', data);
        pos = next;
    }
    if (pos != end) throw runtime_error("неверный размер каталога ZIP");
    return files;
}

map<string, Entry> defaultVfs() {
    map<string, Entry> files = {{"/", {true, ""}}};
    addEntry(files, "/readme.txt", false, "Default VFS\nVariant 22\nStages 1-4\n");
    addEntry(files, "/docs/info.txt", false, "Hello from VFS\nFiles exist only in memory\n");
    addEntry(files, "/docs/course/lab/task.txt", false, "Level 3 directory\n");
    addEntry(files, "/empty", true, "");
    return files;
}


bool isNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isNameChar(char c) {
    return isNameStart(c) || (c >= '0' && c <= '9');
}

string expandVariables(const string& text) {
    string result;
    size_t i = 0;

    while (i < text.size()) {
        if (text[i] == '$' && i + 1 < text.size() && isNameStart(text[i + 1])) {
            size_t start = ++i;
            while (i < text.size() && isNameChar(text[i])) {
                ++i;
            }
            string name = text.substr(start, i - start);
            const char* value = getenv(name.c_str());
            if (value != nullptr) {
                result += value;
            }
        } else {
            result += text[i];
            ++i;
        }
    }
    return result;
}

vector<string> parseLine(const string& line) {
    istringstream input(line);
    vector<string> words;
    string word;

    while (input >> word) {
        words.push_back(expandVariables(word));
    }
    return words;
}

struct Shell {
    map<string, Entry> files;
    string current = "/";
    string name = "vfs22";
};

string requiredPath(const Shell& shell, const string& value) {
    if (value.empty()) throw runtime_error("пустой путь");
    string path = fullPath(value, shell.current);
    string walk = value[0] == '/' ? "/" : shell.current;
    istringstream input(value);
    string part;
    while (getline(input, part, '/')) {
        if (part.empty()) continue;
        if (!shell.files.at(walk).directory) throw runtime_error("не папка: " + walk);
        walk = fullPath(part, walk);
        if (!shell.files.count(walk)) throw runtime_error("путь не найден: " + walk);
    }
    if (value.back() == '/' && !shell.files.at(path).directory) {
        throw runtime_error("не папка: " + path);
    }
    return path;
}

void listFiles(const Shell& shell, const vector<string>& words) {
    bool all = false;
    string value = ".";
    bool hasPath = false;
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i] == "-a") all = true;
        else if (!words[i].empty() && words[i][0] == '-') throw runtime_error("ls: неизвестный флаг");
        else if (hasPath) throw runtime_error("использование: ls [-a] [путь]");
        else { value = words[i]; hasPath = true; }
    }
    string path = requiredPath(shell, value);
    if (!shell.files.at(path).directory) {
        cout << path.substr(path.rfind('/') + 1) << '\n';
        return;
    }
    string prefix = path == "/" ? "/" : path + "/";
    for (const auto& item : shell.files) {
        if (item.first.compare(0, prefix.size(), prefix) != 0) continue;
        string name = item.first.substr(prefix.size());
        if (name.empty() || name.find('/') != string::npos || (!all && name[0] == '.')) continue;
        cout << name << (item.second.directory ? "/" : "") << '\n';
    }
}

int readNumber(const string& text, int low, int high) {
    if (text.empty() || text.find_first_not_of("0123456789") != string::npos) {
        throw runtime_error("ожидалось целое неотрицательное число");
    }
    int result;
    try { result = stoi(text); }
    catch (const exception&) { throw runtime_error("слишком большое число"); }
    if (result < low || result > high) throw runtime_error("число вне допустимого диапазона");
    return result;
}

void headFile(const Shell& shell, const vector<string>& words) {
    int count = 10;
    size_t firstFile = 1;
    if (words.size() > 1 && words[1] == "-n") {
        if (words.size() < 4) throw runtime_error("использование: head [-n число] файл...");
        count = readNumber(words[2], 0, 1000000);
        firstFile = 3;
    }
    if (words.size() <= firstFile) throw runtime_error("head: укажите файл");
    for (size_t i = firstFile; i < words.size(); ++i) {
        if (!words[i].empty() && words[i][0] == '-') throw runtime_error("head: неизвестный флаг");
        string path = requiredPath(shell, words[i]);
        if (shell.files.at(path).directory) throw runtime_error("head: это папка: " + path);
        string text = decode64(shell.files.at(path).data);
        if (text.find('\0') != string::npos) throw runtime_error("head: двоичный файл");
        if (words.size() - firstFile > 1) cout << "==> " << words[i] << " <==\n";
        size_t end = 0;
        for (int line = 0; line < count && end < text.size(); ++line) {
            size_t newline = text.find('\n', end);
            end = newline == string::npos ? text.size() : newline + 1;
        }
        cout << text.substr(0, end);
        if (end > 0 && text[end - 1] != '\n') cout << '\n';
    }
}

void printMonth(int month, int year) {
    const char* names[] = {"January", "February", "March", "April", "May", "June",
                           "July", "August", "September", "October", "November", "December"};
    int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0)) days[1] = 29;
    int previous = year - 1;
    int total = 365 * previous + previous / 4 - previous / 100 + previous / 400;
    for (int i = 0; i < month - 1; ++i) total += days[i];
    int weekday = (total + 1) % 7;
    cout << names[month - 1] << ' ' << year << "\nSu Mo Tu We Th Fr Sa\n";
    for (int i = 0; i < weekday; ++i) cout << "   ";
    for (int day = 1; day <= days[month - 1]; ++day) {
        cout << setw(2) << day;
        if ((weekday + day) % 7 == 0 || day == days[month - 1]) cout << '\n';
        else cout << ' ';
    }
}

void calendar(const vector<string>& words) {
    if (words.size() == 1) {
        time_t now = time(nullptr);
        tm* date = localtime(&now);
        if (!date) throw runtime_error("не удалось получить дату");
        printMonth(date->tm_mon + 1, date->tm_year + 1900);
    } else if (words.size() == 2) {
        int year = readNumber(words[1], 1, 9999);
        for (int month = 1; month <= 12; ++month) printMonth(month, year);
    } else if (words.size() == 3) {
        printMonth(readNumber(words[1], 1, 12), readNumber(words[2], 1, 9999));
    } else throw runtime_error("использование: cal [год] или cal месяц год");
}

void systemName(const vector<string>& words) {
    if (words.size() > 2) throw runtime_error("использование: uname [-s|-n|-r|-v|-m|-a]");
    utsname info{};
    if (uname(&info) != 0) throw runtime_error("не удалось получить сведения об ОС");
    string flag = words.size() == 1 ? "-s" : words[1];
    if (flag == "-s") cout << info.sysname;
    else if (flag == "-n") cout << info.nodename;
    else if (flag == "-r") cout << info.release;
    else if (flag == "-v") cout << info.version;
    else if (flag == "-m") cout << info.machine;
    else if (flag == "-a") {
        cout << info.sysname << ' ' << info.nodename << ' ' << info.release
                  << ' ' << info.version << ' ' << info.machine;
    } else throw runtime_error("uname: неизвестный флаг");
    cout << '\n';
}

bool executeCommand(Shell& shell, const vector<string>& words) {
    if (words.empty()) return true;
    const string& command = words[0];
    if (command == "exit") {
        if (words.size() != 1) throw runtime_error("exit не принимает аргументы");
        cout << "Завершение работы.\n";
        return false;
    }
    if (command == "ls") listFiles(shell, words);
    else if (command == "cd") {
        if (words.size() > 2) throw runtime_error("cd принимает не более одного аргумента");
        string path = requiredPath(shell, words.size() == 1 ? "/" : words[1]);
        if (!shell.files.at(path).directory) throw runtime_error("cd: это не папка");
        shell.current = path;
    } else if (command == "head") headFile(shell, words);
    else if (command == "cal") calendar(words);
    else if (command == "uname") systemName(words);
    else throw runtime_error("неизвестная команда: " + command);
    return true;
}

string removeComment(const string& line) {
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        if (line[i] == '/' && line[i + 1] == '/' &&
            (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) return line.substr(0, i);
    }
    return line;
}

bool runLine(Shell& shell, const string& line) {
    try { return executeCommand(shell, parseLine(removeComment(line))); }
    catch (const exception& error) {
        cout << "Ошибка: " << error.what() << '\n';
        return true;
    }
}

void prompt(const Shell& shell) {
    cout << shell.name << ':' << shell.current << "$ " << flush;
}

int main(int argc, char* argv[]) {
    try {
        string vfsPath, scriptPath;
        bool hasVfs = false, hasScript = false;
        for (int i = 1; i < argc; ++i) {
            string option = argv[i];
            if (option == "--help") {
                cout << "Запуск: emulator [--vfs архив.zip] [--script файл.txt]\n";
                return 0;
            }
            if (option != "--vfs" && option != "--script") throw runtime_error("неизвестный параметр: " + option);
            if (i + 1 == argc || string(argv[i + 1]).empty() ||
                string(argv[i + 1]).rfind("--", 0) == 0) throw runtime_error("нет значения для " + option);
            bool& seen = option == "--vfs" ? hasVfs : hasScript;
            if (seen) throw runtime_error("параметр повторён: " + option);
            seen = true;
            (option == "--vfs" ? vfsPath : scriptPath) = argv[++i];
        }
        cout << "VFS: " << (hasVfs ? vfsPath : "по умолчанию (в памяти)") << '\n';
        cout << "Стартовый скрипт: " << (hasScript ? scriptPath : "не задан") << '\n';
        Shell shell;
        shell.files = hasVfs ? loadZip(vfsPath) : defaultVfs();
        if (hasVfs) shell.name = vfsPath.substr(vfsPath.find_last_of("/\\") + 1);
        if (hasScript) {
            ifstream script(scriptPath);
            if (!script) throw runtime_error("не удалось открыть скрипт: " + scriptPath);
            string line;
            while (getline(script, line)) {
                prompt(shell);
                cout << line << '\n';
                if (!runLine(shell, line)) return 0;
            }
            if (script.bad()) throw runtime_error("ошибка чтения скрипта");
        }
        string line;
        while (true) {
            prompt(shell);
            if (!getline(cin, line)) { cout << '\n'; break; }
            if (!runLine(shell, line)) break;
        }
        return 0;
    } catch (const exception& error) {
        cerr << "Ошибка запуска: " << error.what() << '\n';
        return 1;
    }
}
