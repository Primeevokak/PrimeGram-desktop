# PrimeGram Desktop

<p align="center">
  <img src="Telegram/Resources/art/icon512.png" alt="PrimeGram Logo" width="128" height="128" />
</p>

<p align="center">
  <b>Современный, быстрый и кастомизированный клиент Telegram для Windows с расширенными возможностями приватности, управления памятью и ИИ-интеграцией.</b>
</p>

<p align="center">
  <a href="https://github.com/Primeevokak/PrimeGram-desktop/releases"><img src="https://img.shields.io/github/v/release/Primeevokak/PrimeGram-desktop?color=blue&label=PrimeGram" alt="Release" /></a>
  <a href="https://github.com/Primeevokak/PrimeGram-desktop"><img src="https://img.shields.io/badge/Telegram%20Core-v7.4%20(Layer%20231)-0088cc" alt="Telegram Core" /></a>
  <a href="https://github.com/Primeevokak/PrimeGram-desktop"><img src="https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011%20x64-0078d4" alt="Platform" /></a>
  <a href="https://github.com/Primeevokak/PrimeGram-desktop/blob/dev/LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-green" alt="License" /></a>
</p>

---

## 🌟 Ключевые возможности PrimeGram

### 1. 🛡️ Биометрический вход через Windows Hello
- **Безопасность уровня ОС**: разблокировка приложения по отпечатку пальца, скану лица или Windows PIN-коду.
- **Холодный запуск**: в отличие от стандартного клиента, Windows Hello доступен даже при полном перезапуске приложения (пароль защищается аппаратным шифрованием Windows DPAPI / Credential Manager).
- **Мгновенный доступ**: биометрический запрос открывается автоматически при старте и разблокировке экрана.

### 2. 🧹 Продвинутый менеджер кэша (Cache Manager)
- **Категоризированный аудит памяти**: раздельный подсчёт размера фото, видео/документов, стикеров/GIF, аудио/голосовых и временных файлов в фоновом потоке.
- **Выборочная очистка**: очищайте только то, что занимает лишнее место, не удаляя нужные медиафайлы.
- **Полная очистка в 1 клик**: мгновенный сброс всех кэшей базы данных `Storage::Cache::Database`.

### 3. 🎵 Меню «Сейчас играет» (Now Playing)
- Быстрая отправка текущего воспроизводимого трека прямо из меню вложений скрепки в любом чате.
- Автоматическое определение названия трека и исполнителя через Windows Media Session Manager.

### 4. 🎨 Тонкая настройка интерфейса
- **Точные счётчики**: отображение точного количества подписчиков, просмотров и реакций без округления до «k/m».
- **Секунды в сообщениях**: точное время отправки вплоть до секунды.
- **Кастомный статус редактирования**: изящная иконка карандаша у отредактированных сообщений.
- **Относительное время захода**: удобный формат «был(а) в сети 5 мин. назад» вместо сухого времени.
- **Скрытие реакций и историй**: возможность отключить визуальный шум в чатах.

### 5. 🤖 Встроенный MCP Server (Model Context Protocol)
- Локальный JSON-RPC сервер на `127.0.0.1:28888` для взаимодействия с ИИ-ассистентами (Cursor, Antigravity, Claude Desktop).
- Безопасная модель прав: чтение истории и поиск доступны по умолчанию, отправка сообщений и файлов защищена индивидуальными переключателями в настройках.

### 6. 🚀 Автообновления через GitHub Releases
- Автоматическая проверка свежих версий прямо из официального репозитория [Primeevokak/PrimeGram-desktop](https://github.com/Primeevokak/PrimeGram-desktop).
- Защита от перезаписи официальным клиентом Telegram.

---

## 🛠️ Сборка из исходного кода (Windows x64)

### Системные требования:
- **ОС**: Windows 10 (21H2+) или Windows 11 x64
- **Компилятор**: Visual Studio 2022 (MSVC v143, сборка C++ x64)
- **Инструменты**: Python 3.11+, CMake 3.25+, Ninja
- **Библиотеки**: Qt 5.15.19 x64 static, OpenSSL 3, WebRTC

### Инструкция по сборке:

1. **Клонирование репозитория:**
   ```bash
   git clone --recursive https://github.com/Primeevokak/PrimeGram-desktop.git
   cd PrimeGram-desktop
   ```

2. **Подготовка окружения:**
   ```cmd
   build_prepare.bat
   ```

3. **Конфигурация CMake:**
   ```cmd
   build_telegram.bat configure
   ```

4. **Сборка бинарного файла:**
   - Для сборки **Debug** (для разработки и отладки):
     ```cmd
     build_telegram.bat build
     ```
   - Для сборки **Release** (оптимизированный исполняемый файл):
     ```cmd
     build_telegram.bat release
     ```
   - Для сборки и **упаковки релиза** (с цифровой подписью Prime INC и созданием архива для GitHub Releases):
     ```cmd
     build_telegram.bat package
     ```
   - Для отдельной **подписи бинарника**:
     ```cmd
     build_telegram.bat sign
     ```

Готовый файл `Telegram.exe` создаётся в папке:
- `out/Debug/Telegram.exe` (Debug)
- `out/Release/Telegram.exe` (Release)

Готовый релизный архив для GitHub Releases:
- `releases/PrimeGram-v7.4-x64-portable.zip`
- `releases/SHA256SUMS.txt`

---

## 📂 Структура Prime-компонентов

Все кастомные функции PrimeGram изолированы в директории `Telegram/SourceFiles/prime/`:

```
Telegram/SourceFiles/
├── prime/
│   ├── cache/             # Движок аудита и выборочной очистки кэша
│   ├── privacy/           # Windows Hello, DPAPI и безопасность
│   ├── media/             # «Сейчас играет», селектор качества и эффекты
│   ├── mcp/               # Встроенный MCP Server
│   ├── updates/           # Автообновление через GitHub Releases
│   └── prime_settings.h   # Глобальные настройки PrimeGram
└── settings/sections/
    ├── settings_prime.cpp           # Главный раздел настроек PrimeGram
    ├── settings_prime_storage.cpp   # Страница управления памятью и кэшем
    ├── settings_prime_privacy.cpp   # Настройки Windows Hello и защиты
    └── settings_prime_interface.cpp # Твики внешнего вида и сообщений
```

---

## 📄 Лицензия

Исходный код PrimeGram Desktop распространяется под лицензией **GNU General Public License v3.0** с исключением для OpenSSL в соответствии с лицензией Telegram Desktop.
