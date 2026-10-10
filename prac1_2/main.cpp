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

struct Entry {
    bool directory;
    std::string data;
};

const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode64(const std::string& text) {
    std::string result;
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

std::string decode64(const std::string& text) {
    std::string result;
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

std::string fullPath(const std::string& path, const std::string& current = "/") {
    std::string source = (!path.empty() && path[0] == '/') ? path : current + "/" + path;
    std::vector<std::string> parts;
    std::string part;
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
    std::string result;
    for (const auto& item : parts) result += "/" + item;
    return result.empty() ? "/" : result;
}

void addEntry(std::map<std::string, Entry>& files, const std::string& path,
              bool directory, const std::string& data) {
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        std::string parent = path.substr(0, i);
        if (files.count(parent) && !files[parent].directory) {
            throw std::runtime_error("файл используется как папка: " + parent);
        }
        files[parent] = {true, ""};
    }
    if (files.count(path) && (!directory || !files[path].directory)) {
        throw std::runtime_error("повторяющийся путь в ZIP: " + path);
    }
    files[path] = {directory, directory ? "" : encode64(data)};
}

std::uint32_t zipNumber(const std::string& bytes, std::size_t pos, std::size_t count) {
    if (pos > bytes.size() || count > bytes.size() - pos) {
        throw std::runtime_error("повреждённый ZIP: данные обрезаны");
    }
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < count; ++i) {
        result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + i])) << (8 * i);
    }
    return result;
}

std::string unzipData(const std::string& bytes, std::size_t pos, std::size_t packed,
                      std::size_t size, unsigned int method) {
    if (pos > bytes.size() || packed > bytes.size() - pos) {
        throw std::runtime_error("повреждённые данные файла в ZIP");
    }
    if (method == 0) {
        if (packed != size) throw std::runtime_error("неверный размер файла в ZIP");
        return bytes.substr(pos, size);
    }
    if (method != 8) throw std::runtime_error("поддерживаются ZIP Store и Deflate");
    std::string result(size + 1, '\0');
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data() + pos));
    stream.avail_in = static_cast<uInt>(packed);
    stream.next_out = reinterpret_cast<Bytef*>(&result[0]);
    stream.avail_out = static_cast<uInt>(result.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        throw std::runtime_error("не удалось запустить распаковку в память");
    }
    int status = inflate(&stream, Z_FINISH);
    bool valid = status == Z_STREAM_END && stream.total_out == size && stream.total_in == packed;
    inflateEnd(&stream);
    if (!valid) throw std::runtime_error("ошибка распаковки ZIP");
    result.resize(size);
    return result;
}

std::map<std::string, Entry> loadZip(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("не удалось открыть VFS: " + filename);
    const std::size_t limit = 64 * 1024 * 1024;
    if (input.tellg() < 0 || input.tellg() > static_cast<std::streamoff>(limit)) {
        throw std::runtime_error("ZIP слишком большой: предел 64 МиБ");
    }
    input.seekg(0);
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() < 22) throw std::runtime_error("файл не является ZIP-архивом");
    std::size_t end = bytes.size() - 22;
    std::size_t first = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
    while (true) {
        if (zipNumber(bytes, end, 4) == 0x06054b50 &&
            end + 22 + zipNumber(bytes, end + 20, 2) == bytes.size()) break;
        if (end == first) throw std::runtime_error("не найден конец ZIP-архива");
        --end;
    }
    unsigned int count = zipNumber(bytes, end + 10, 2);
    std::size_t pos = zipNumber(bytes, end + 16, 4);
    std::size_t centralSize = zipNumber(bytes, end + 12, 4);
    if (zipNumber(bytes, end + 4, 2) || zipNumber(bytes, end + 6, 2) ||
        zipNumber(bytes, end + 8, 2) != count || count == 65535 || pos > end || centralSize != end - pos) {
        throw std::runtime_error("многотомный ZIP, ZIP64 или повреждённый архив не поддерживается");
    }
    std::map<std::string, Entry> files = {{"/", {true, ""}}};
    std::size_t total = 0;
    for (unsigned int i = 0; i < count; ++i) {
        if (zipNumber(bytes, pos, 4) != 0x02014b50) throw std::runtime_error("ошибка каталога ZIP");
        unsigned int flags = zipNumber(bytes, pos + 8, 2);
        unsigned int method = zipNumber(bytes, pos + 10, 2);
        std::size_t packed = zipNumber(bytes, pos + 20, 4);
        std::size_t size = zipNumber(bytes, pos + 24, 4);
        std::size_t nameSize = zipNumber(bytes, pos + 28, 2);
        std::size_t next = pos + 46 + nameSize + zipNumber(bytes, pos + 30, 2) + zipNumber(bytes, pos + 32, 2);
        std::size_t local = zipNumber(bytes, pos + 42, 4);
        unsigned int mode = zipNumber(bytes, pos + 38, 4) >> 16;
        if (flags & 1) throw std::runtime_error("ZIP с паролем не поддерживается");
        if ((mode & 0170000) == 0120000) throw std::runtime_error("символические ссылки не поддерживаются");
        if (next > end || nameSize == 0 || size > limit - total) {
            throw std::runtime_error("повреждённый ZIP или размер VFS больше 64 МиБ");
        }
        total += size;
        std::string name = bytes.substr(pos + 46, nameSize);
        if (name[0] == '/' || name.find('\0') != std::string::npos ||
            name.find('\\') != std::string::npos || ("/" + name + "/").find("/../") != std::string::npos) {
            throw std::runtime_error("недопустимый путь в ZIP");
        }
        if (zipNumber(bytes, local, 4) != 0x04034b50 ||
            zipNumber(bytes, local + 8, 2) != method || zipNumber(bytes, local + 6, 2) != flags) {
            throw std::runtime_error("ошибка заголовка ZIP");
        }
        std::size_t localName = zipNumber(bytes, local + 26, 2);
        std::size_t dataPos = local + 30 + localName + zipNumber(bytes, local + 28, 2);
        if (bytes.substr(local + 30, localName) != name || dataPos > pos || packed > pos - dataPos) {
            throw std::runtime_error("неверное расположение данных ZIP");
        }
        std::string data = unzipData(bytes, dataPos, packed, size, method);
        auto crc = crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
        if (crc != zipNumber(bytes, pos + 16, 4)) throw std::runtime_error("не совпала контрольная сумма ZIP");
        addEntry(files, fullPath(name), name.back() == '/', data);
        pos = next;
    }
    if (pos != end) throw std::runtime_error("неверный размер каталога ZIP");
    return files;
}

std::map<std::string, Entry> defaultVfs() {
    std::map<std::string, Entry> files = {{"/", {true, ""}}};
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

std::string expandVariables(const std::string& text) {
    std::string result;
    std::size_t i = 0;

    while (i < text.size()) {
        if (text[i] == '$' && i + 1 < text.size() && isNameStart(text[i + 1])) {
            std::size_t start = ++i;
            while (i < text.size() && isNameChar(text[i])) {
                ++i;
            }
            std::string name = text.substr(start, i - start);
            const char* value = std::getenv(name.c_str());
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

std::vector<std::string> parseLine(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> words;
    std::string word;

    while (input >> word) {
        words.push_back(expandVariables(word));
    }
    return words;
}

struct Shell {
    std::map<std::string, Entry> files;
    std::string current = "/";
    std::string name = "vfs22";
};

std::string requiredPath(const Shell& shell, const std::string& value) {
    if (value.empty()) throw std::runtime_error("пустой путь");
    std::string path = fullPath(value, shell.current);
    std::string walk = value[0] == '/' ? "/" : shell.current;
    std::istringstream input(value);
    std::string part;
    while (std::getline(input, part, '/')) {
        if (part.empty()) continue;
        if (!shell.files.at(walk).directory) throw std::runtime_error("не папка: " + walk);
        walk = fullPath(part, walk);
        if (!shell.files.count(walk)) throw std::runtime_error("путь не найден: " + walk);
    }
    if (value.back() == '/' && !shell.files.at(path).directory) {
        throw std::runtime_error("не папка: " + path);
    }
    return path;
}

void listFiles(const Shell& shell, const std::vector<std::string>& words) {
    bool all = false;
    std::string value = ".";
    bool hasPath = false;
    for (std::size_t i = 1; i < words.size(); ++i) {
        if (words[i] == "-a") all = true;
        else if (!words[i].empty() && words[i][0] == '-') throw std::runtime_error("ls: неизвестный флаг");
        else if (hasPath) throw std::runtime_error("использование: ls [-a] [путь]");
        else { value = words[i]; hasPath = true; }
    }
    std::string path = requiredPath(shell, value);
    if (!shell.files.at(path).directory) {
        std::cout << path.substr(path.rfind('/') + 1) << '\n';
        return;
    }
    std::string prefix = path == "/" ? "/" : path + "/";
    for (const auto& item : shell.files) {
        if (item.first.compare(0, prefix.size(), prefix) != 0) continue;
        std::string name = item.first.substr(prefix.size());
        if (name.empty() || name.find('/') != std::string::npos || (!all && name[0] == '.')) continue;
        std::cout << name << (item.second.directory ? "/" : "") << '\n';
    }
}

int readNumber(const std::string& text, int low, int high) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error("ожидалось целое неотрицательное число");
    }
    int result;
    try { result = std::stoi(text); }
    catch (const std::exception&) { throw std::runtime_error("слишком большое число"); }
    if (result < low || result > high) throw std::runtime_error("число вне допустимого диапазона");
    return result;
}

void headFile(const Shell& shell, const std::vector<std::string>& words) {
    int count = 10;
    std::size_t firstFile = 1;
    if (words.size() > 1 && words[1] == "-n") {
        if (words.size() < 4) throw std::runtime_error("использование: head [-n число] файл...");
        count = readNumber(words[2], 0, 1000000);
        firstFile = 3;
    }
    if (words.size() <= firstFile) throw std::runtime_error("head: укажите файл");
    for (std::size_t i = firstFile; i < words.size(); ++i) {
        if (!words[i].empty() && words[i][0] == '-') throw std::runtime_error("head: неизвестный флаг");
        std::string path = requiredPath(shell, words[i]);
        if (shell.files.at(path).directory) throw std::runtime_error("head: это папка: " + path);
        std::string text = decode64(shell.files.at(path).data);
        if (text.find('\0') != std::string::npos) throw std::runtime_error("head: двоичный файл");
        if (words.size() - firstFile > 1) std::cout << "==> " << words[i] << " <==\n";
        std::size_t end = 0;
        for (int line = 0; line < count && end < text.size(); ++line) {
            std::size_t newline = text.find('\n', end);
            end = newline == std::string::npos ? text.size() : newline + 1;
        }
        std::cout << text.substr(0, end);
        if (end > 0 && text[end - 1] != '\n') std::cout << '\n';
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
    std::cout << names[month - 1] << ' ' << year << "\nSu Mo Tu We Th Fr Sa\n";
    for (int i = 0; i < weekday; ++i) std::cout << "   ";
    for (int day = 1; day <= days[month - 1]; ++day) {
        std::cout << std::setw(2) << day;
        if ((weekday + day) % 7 == 0 || day == days[month - 1]) std::cout << '\n';
        else std::cout << ' ';
    }
}

void calendar(const std::vector<std::string>& words) {
    if (words.size() == 1) {
        std::time_t now = std::time(nullptr);
        std::tm* date = std::localtime(&now);
        if (!date) throw std::runtime_error("не удалось получить дату");
        printMonth(date->tm_mon + 1, date->tm_year + 1900);
    } else if (words.size() == 2) {
        int year = readNumber(words[1], 1, 9999);
        for (int month = 1; month <= 12; ++month) printMonth(month, year);
    } else if (words.size() == 3) {
        printMonth(readNumber(words[1], 1, 12), readNumber(words[2], 1, 9999));
    } else throw std::runtime_error("использование: cal [год] или cal месяц год");
}

void systemName(const std::vector<std::string>& words) {
    if (words.size() > 2) throw std::runtime_error("использование: uname [-s|-n|-r|-v|-m|-a]");
    utsname info{};
    if (uname(&info) != 0) throw std::runtime_error("не удалось получить сведения об ОС");
    std::string flag = words.size() == 1 ? "-s" : words[1];
    if (flag == "-s") std::cout << info.sysname;
    else if (flag == "-n") std::cout << info.nodename;
    else if (flag == "-r") std::cout << info.release;
    else if (flag == "-v") std::cout << info.version;
    else if (flag == "-m") std::cout << info.machine;
    else if (flag == "-a") {
        std::cout << info.sysname << ' ' << info.nodename << ' ' << info.release
                  << ' ' << info.version << ' ' << info.machine;
    } else throw std::runtime_error("uname: неизвестный флаг");
    std::cout << '\n';
}

bool executeCommand(Shell& shell, const std::vector<std::string>& words) {
    if (words.empty()) return true;
    const std::string& command = words[0];
    if (command == "exit") {
        if (words.size() != 1) throw std::runtime_error("exit не принимает аргументы");
        std::cout << "Завершение работы.\n";
        return false;
    }
    if (command == "ls") listFiles(shell, words);
    else if (command == "cd") {
        if (words.size() > 2) throw std::runtime_error("cd принимает не более одного аргумента");
        std::string path = requiredPath(shell, words.size() == 1 ? "/" : words[1]);
        if (!shell.files.at(path).directory) throw std::runtime_error("cd: это не папка");
        shell.current = path;
    } else if (command == "head") headFile(shell, words);
    else if (command == "cal") calendar(words);
    else if (command == "uname") systemName(words);
    else throw std::runtime_error("неизвестная команда: " + command);
    return true;
}

std::string removeComment(const std::string& line) {
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        if (line[i] == '/' && line[i + 1] == '/' &&
            (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) return line.substr(0, i);
    }
    return line;
}

bool runLine(Shell& shell, const std::string& line) {
    try { return executeCommand(shell, parseLine(removeComment(line))); }
    catch (const std::exception& error) {
        std::cout << "Ошибка: " << error.what() << '\n';
        return true;
    }
}

void prompt(const Shell& shell) {
    std::cout << shell.name << ':' << shell.current << "$ " << std::flush;
}

int main(int argc, char* argv[]) {
    try {
        std::string vfsPath, scriptPath;
        bool hasVfs = false, hasScript = false;
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--help") {
                std::cout << "Запуск: emulator [--vfs архив.zip] [--script файл.txt]\n";
                return 0;
            }
            if (option != "--vfs" && option != "--script") throw std::runtime_error("неизвестный параметр: " + option);
            if (i + 1 == argc || std::string(argv[i + 1]).empty() ||
                std::string(argv[i + 1]).rfind("--", 0) == 0) throw std::runtime_error("нет значения для " + option);
            bool& seen = option == "--vfs" ? hasVfs : hasScript;
            if (seen) throw std::runtime_error("параметр повторён: " + option);
            seen = true;
            (option == "--vfs" ? vfsPath : scriptPath) = argv[++i];
        }
        std::cout << "VFS: " << (hasVfs ? vfsPath : "по умолчанию (в памяти)") << '\n';
        std::cout << "Стартовый скрипт: " << (hasScript ? scriptPath : "не задан") << '\n';
        Shell shell;
        shell.files = hasVfs ? loadZip(vfsPath) : defaultVfs();
        if (hasVfs) shell.name = vfsPath.substr(vfsPath.find_last_of("/\\") + 1);
        if (hasScript) {
            std::ifstream script(scriptPath);
            if (!script) throw std::runtime_error("не удалось открыть скрипт: " + scriptPath);
            std::string line;
            while (std::getline(script, line)) {
                prompt(shell);
                std::cout << line << '\n';
                if (!runLine(shell, line)) return 0;
            }
            if (script.bad()) throw std::runtime_error("ошибка чтения скрипта");
        }
        std::string line;
        while (true) {
            prompt(shell);
            if (!std::getline(std::cin, line)) { std::cout << '\n'; break; }
            if (!runLine(shell, line)) break;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка запуска: " << error.what() << '\n';
        return 1;
    }
}
