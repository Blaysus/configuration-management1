import os
import shlex


def start_cli(vfs_name):
    print(f"Эмулятор виртуальной файловой системы: {vfs_name}")
    print("Доступные команды: ls, cd, exit\n")

    while True:
        try:
            # Получаем команду пользователя
            user_input = input(f"{vfs_name} $> ").strip()

            if not user_input:
                continue

            # Разделяем строку на команду и аргументы
            parts = shlex.split(user_input)

            # Раскрываем переменные окружения
            parts = [os.path.expandvars(part) for part in parts]

            command = parts[0]
            args = parts[1:]

            # Команда завершения
            if command == "exit":
                if args:
                    print("Ошибка: команда exit не принимает аргументы")
                else:
                    print("Завершение работы.")
                    break

            # Заглушка ls
            elif command == "ls":
                print(f"Команда: {command}")
                print(f"Аргументы: {args}")

            # Заглушка cd
            elif command == "cd":
                print(f"Команда: {command}")
                print(f"Аргументы: {args}")

            # Неизвестная команда
            else:
                print(f"Ошибка: неизвестная команда '{command}'")

        except ValueError:
            print("Ошибка: неправильный формат команды")

        except (KeyboardInterrupt, EOFError):
            print("\nЗавершение работы.")
            break


if __name__ == "__main__":
    start_cli("root.zip")