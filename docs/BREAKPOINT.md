# Breakpoint: читання текстів у MVP 0.2

## Підтверджений обсяг

Перевірено на локальній Steam-інсталяції **appid 2231380, buildid 25349072**: v27 `.forge`, англійські `LocalizationPackage_English(US)*`, стиснений `.data` v3 / algorithm 3 та обидва спостережені заголовки локалізації (інтерфейс і субтитри).

Із 23 прочитаних версій пакетів утворюються 22 ефективні: при однаковому package ID для цього експорту обирається запис із `DataPC_patch_01.forge`, а не базовий. Чотири пакети порожні. Результат — **76 732 записи**. Серед субтитрів є також службові, авторські й контекстні рядки: це всі рядки вибраних ресурсів, а не обіцянка, що кожен показується гравцеві.

Це **не загальна підтримка `.forge`**: інші ігри, версії, типи ресурсів, мови, стиснення та компонування не перевірено. Є окремий [експериментальний тестовий writer](TEST-PATCH.md), але немає підтвердженого в грі загального пакувальника або інсталятора перекладу. Саму гру для тесту зміненого перекладу не запускали.

## Готовий проєкт

У результатах цього сеансу є `breakpoint-project/Breakpoint-English-to-Ukrainian.als.json`. Відкрийте його в Studio через **Проєкт → Відкрити JSON…**. Це вже витягнуті оригінали, без українських перекладів: `translation` порожній, `status=todo`, мови `en-US → uk`.

Стабільний ID редактора: `grb:<package class ID>:<string ID>`. Ідентифікатор рядка = базовий uint32 ID блока + uint16 delta. Контекст містить архів, назву пакета, початковий ID, базу блока й delta. ID не потрібно змінювати під час перекладу.

## Збірка для Arch Linux

Редактор збирається як раніше. Додатково для витягування потрібні Wine та MinGW-w64 (helper — маленька Windows-програма, бібліотека гри — Win64 DLL):

```sh
sudo pacman -Syu --needed base-devel cmake ninja qt6-base wine mingw-w64-gcc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
mkdir -p "$PWD/projects"
./build/anvil-extract-breakpoint \
  --game-dir "/path/to/SteamLibrary/steamapps/common/Ghost Recon Breakpoint" \
  --output "$PWD/projects/Breakpoint-English-to-Ukrainian.als.json" \
  --wine-prefix "$PWD/.anvil-wine-prefix"
./build/anvil-studio --open "$PWD/projects/Breakpoint-English-to-Ukrainian.als.json"
```

Перший запуск створює окремий префікс Wine за вказаним шляхом. Якщо MinGW не було під час конфігурації, встановіть його та повторіть `cmake -S … -B …`; без helper редактор працює, але декомпресія недоступна.

## Windows

Збірка MSVC та `windeployqt` описані в головному README. Helper збирається автоматично як `anvil-oodle-helper.exe`. Wine та MinGW на Windows не потрібні.

```powershell
New-Item -ItemType Directory -Force projects
& .\build\Release\anvil-extract-breakpoint.exe `
  --game-dir "D:\SteamLibrary\steamapps\common\Ghost Recon Breakpoint" `
  --output "$PWD\projects\Breakpoint-English-to-Ukrainian.als.json"
& .\build\Release\anvil-studio.exe --open "$PWD\projects\Breakpoint-English-to-Ukrainian.als.json"
```

Windows-код підготовлено, але на Windows-хості в цьому сеансі не перевірено. На Arch перевірено і редактор, і Windows helper під Wine.

## Правила утиліти

- Читає тільки `DataPC.forge`, `DataPC_patch_01.forge` та явно вказану бібліотеку декомпресії. Не обходить усі ресурси гри й не розпаковує десятки гігабайтів.
- DLL береться з вашої інсталяції (`oo2core_7_win64.dll`) та копіюється у тимчасову папку для helper. Власницька DLL **не включена** до вихідного коду чи Linux-пакета. За потреби задайте `--codec-dll`.
- Ігрові EXE не запускаються. Працює власний helper, який викликає тільки `OodleLZ_Decompress` у копії DLL.
- Новий проєкт і Wine-префікс повинні бути поза папкою гри. Перевірка враховує `..`, наявні символьні посилання й найближчого існуючого предка.
- Вихідний проєкт створюється тільки як новий файл: наявний файл утиліта не перезаписує. Для повторного витягування оберіть іншу назву.
- Тимчасові файли очищуються. Префікс Wine залишається для повторних запусків. Для нестандартного розміщення helper задайте `--helper`.
- Невідома версія, обрізаний запис, неправильна контрольна сума, цикл граматики, некоректний UTF-16, дублікати ID, пропущені/зайві байти й перевищення лімітів зупиняють експорт.

## Формат декодованого пакета

Меню імпорту та Qt-плагін `grb.localizationpayload.v1` приймають **байти ресурсу після декомпресії та зняття контейнерної рамки**, з його 41-байтовим заголовком. Розширення Studio — `.localizationpayload`. Це не універсальний формат ATK і не стиснений `.data` або цілий `.forge`.

На досліджених файлах: resource type `0x6E3C9C6F`; marker за зсувом 33 — `B5 89 83 D2`; uint32 LE за зсувом 37 — розмір тіла. Далі — big-endian словник Re-Pair, 12-байтові записи каталогу `(uint32 baseID, uint32 textOffset, uint32 limitsOffset)`, межі рядків і послідовності символів. Порожній пакет має нульову довжину тіла.

Читач потребує повного покриття байтів тіла без перетинів. Він не шукає довільні «схожі на текст» ділянки й не приховує часткове декодування.

## Джерела й перевірка

Власний C++ читач звірено з незалежним дослідницьким Python-читачем на всіх 23 локальних зразках: **97 156 рядків**, включно з базовою й оновленою версіями головного пакета, збігаються за текстом та ID. JSON і CSV проходять зворотне читання без втрат.

Опис контейнерів досліджувався за первинним [data_inspect.py](https://github.com/dataterminals/grb-modding-knowledgebase/blob/main/tools/data_inspect.py), індексів — за [forge_inspect.py](https://github.com/dataterminals/grb-modding-knowledgebase/blob/main/tools/forge_inspect.py). Приклад граматики надано автором G14VVH5 у [patch02_toolkit.py](https://github.com/G14VVH5/ac-bf-resynced-localization-kit/blob/main/patch02_toolkit.py). Логіку реалізовано окремо з перевірками меж. Відмінності Breakpoint від цих прикладів перевірялися на локальних байтах; сумісність інших ігор не припускалася.

Див. [результати перевірки](VALIDATION.md).
