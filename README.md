# Практическое занятие №1

## Задание 1

В терминале:

```bash
cut -d: -f1 /etc/passwd | sort
```

---

## Задание 2

В терминале:

```bash
cd /etc
awk '{print $2, $1}' protocols | tail -5 | sort -r
```

---

## Задание 3

В терминале:

```bash
cd ~
mkdir -p pract1
cd pract1
nano banner
```

В `nano banner`:

```bash
#!/bin/bash

text="$*"
length=${#text}

line=$(printf '%*s' "$((length + 2))" '' | tr ' ' '-')

echo "+$line+"
echo "| $text |"
echo "+$line+"
```

После сохранения файла:

```bash
chmod +x banner
./banner "Hello from RTU MIREA!"
```

---

## Задание 4

В терминале:

```bash
nano identifiers
```

В `nano identifiers`:

```bash
#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Использование: ./identifiers файл"
    exit 1
fi

grep -oE '[A-Za-z_][A-Za-z0-9_]*' "$1" | sort -u | tr '\n' ' '
echo
```

После сохранения:

```bash
chmod +x identifiers
nano hello.c
```

В `nano hello.c`:

```c
#include <stdio.h>

int main()
{
    printf("hello world\n");
    return 0;
}
```

Запуск:

```bash
./identifiers hello.c
```

---

## Задание 5

В терминале:

```bash
nano reg
```

В `nano reg`:

```bash
#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Использование: ./reg программа"
    exit 1
fi

if [ ! -f "$1" ]; then
    echo "Файл не найден"
    exit 1
fi

chmod 755 "$1"
sudo cp "$1" /usr/local/bin/

echo "Программа $1 установлена"
```

После сохранения:

```bash
chmod +x reg
./reg banner
```

После регистрации `banner` можно запустить как обычную команду:

```bash
banner "Hello from RTU MIREA!"
```

---

## Задание 6

В терминале:

```bash
nano checkcomments
```

В `nano checkcomments`:

```bash
#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Использование: ./checkcomments каталог"
    exit 1
fi

find "$1" -type f \( -name "*.c" -o -name "*.js" -o -name "*.py" \) | while read file
do
    first=$(head -n 1 "$file")

    case "$file" in
        *.py)
            if echo "$first" | grep -q '^#'; then
                echo "$file - комментарий есть"
            else
                echo "$file - комментария нет"
            fi
            ;;

        *.c|*.js)
            if echo "$first" | grep -qE '^//|^/\*'; then
                echo "$file - комментарий есть"
            else
                echo "$file - комментария нет"
            fi
            ;;
    esac
done
```

После сохранения:

```bash
chmod +x checkcomments
./checkcomments .
```

---

## Задание 7

В терминале:

```bash
nano duplicates
```

В `nano duplicates`:

```bash
#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Использование: ./duplicates каталог"
    exit 1
fi

find "$1" -type f -exec md5sum {} + | sort | uniq -w 32 -D
```

После сохранения:

```bash
chmod +x duplicates
./duplicates .
```

---

## Задание 8

В терминале:

```bash
nano archive_ext
```

В `nano archive_ext`:

```bash
#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Использование: ./archive_ext расширение"
    exit 1
fi

find . -maxdepth 1 -type f -name "*.$1" -print | tar -cf archive.tar -T -

echo "Архив archive.tar создан"
```

После сохранения:

```bash
chmod +x archive_ext
./archive_ext txt
```

Посмотреть содержимое архива:

```bash
tar -tf archive.tar
```

---

## Задание 9

В терминале:

```bash
nano spaces_to_tabs
```

В `nano spaces_to_tabs`:

```bash
#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Использование: ./spaces_to_tabs входной_файл выходной_файл"
    exit 1
fi

sed $'s/    /\t/g' "$1" > "$2"

echo "Замена выполнена"
```

После сохранения:

```bash
chmod +x spaces_to_tabs
```

Запуск:

```bash
./spaces_to_tabs input.txt output.txt
```

Для проверки табуляции:

```bash
cat -T output.txt
```

---

## Задание 10

В терминале:

```bash
nano emptyfiles
```

В `nano emptyfiles`:

```bash
#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Использование: ./emptyfiles директория"
    exit 1
fi

find "$1" -maxdepth 1 -type f -empty -printf '%f\n'
```

После сохранения:

```bash
chmod +x emptyfiles
./emptyfiles .
```