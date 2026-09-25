// Every text of the interface, in Ukrainian and English.
//
// STR(id, Ukrainian, English). Included by Lang.h (the ids) and Lang.cpp (the
// table), so a text and its translations always stay on the same entry.
// Format strings must take the same arguments in the same order in both
// languages (tests/test_game.cpp checks it).

// ---- Languages (each in its own language, for the «Мова» menu) ----------------
STR(kStrLanguageSelf, L"&Українська", L"&English")

// ---- Main menu ------------------------------------------------------------------
STR(kStrMenuGame, L"&Гра", L"&Game")
STR(kStrMenuNew, L"&Нова гра…\tCtrl+N", L"&New game…\tCtrl+N")
STR(kStrMenuSave, L"&Зберегти\tCtrl+S", L"&Save\tCtrl+S")
STR(kStrMenuSaveAs, L"Зберегти &як…\tCtrl+Shift+S", L"Save &as…\tCtrl+Shift+S")
STR(kStrMenuLoad, L"З&авантажити…\tCtrl+O", L"&Load…\tCtrl+O")
STR(kStrMenuExit, L"&Вихід\tAlt+F4", L"E&xit\tAlt+F4")
STR(kStrMenuNetwork, L"&Мережа", L"&Network")
STR(kStrMenuNetHost, L"&Створити мережеву гру…", L"&Host a network game…")
STR(kStrMenuNetHostSaved, L"Створити мережеву гру зі &збереження…",
    L"Host a network game from a &save…")
STR(kStrMenuNetJoin, L"&Приєднатися до гри…", L"&Join a game…")
STR(kStrMenuNetLeave, L"&Вийти з мережевої гри", L"&Leave the network game")
STR(kStrMenuLanguage, L"Мо&ва", L"&Language")
STR(kStrMenuHelp, L"&Довідка", L"&Help")
STR(kStrMenuRules, L"&Правила гри\tF1", L"&Rules\tF1")
STR(kStrMenuAbout, L"Про &програму", L"&About Feodals")

// ---- Window title -----------------------------------------------------------------
STR(kStrNewGame, L"Нова гра", L"New game")
STR(kStrTitleHosting, L" (мережева гра, сервер)", L" (network game, host)")
STR(kStrNetworkGame, L"Мережева гра", L"Network game")
STR(kStrTitleNetworkEnded, L"Мережева гра (завершено)", L"Network game (ended)")

// ---- Side panel ---------------------------------------------------------------------
STR(kStrPanelGameOver, L"Гру завершено", L"Game over")
STR(kStrPanelWaiting, L"Очікування гравців", L"Waiting for players")
STR(kStrPanelPaused, L"Пауза", L"Paused")
STR(kStrPanelPlayers, L"Гравці", L"Players")
STR(kStrFreeSeat, L"Вільне місце", L"Free seat")
STR(kStrYouSuffix, L" (ви)", L" (you)")
STR(kStrDisconnectedWaiting, L"від'єднався, очікуємо…", L"disconnected, waiting…")
STR(kStrWaitingForPlayer, L"очікуємо на гравця…", L"waiting for a player…")
STR(kStrJoined, L"приєднався", L"joined")
STR(kStrPanelBoard, L"Поле: %s × %s", L"Board: %s × %s")
STR(kStrPanelClaimed, L"Зафарбовано: %s (%s)", L"Claimed: %s (%s)")
STR(kStrPanelFree, L"Вільних: %s", L"Free: %s")
STR(kStrPanelHostPort, L"Мережева гра · сервер, порт %u",
    L"Network game · host, port %u")
STR(kStrPanelClient, L"Мережева гра · клієнт", L"Network game · client")
STR(kStrPanelNetEnded, L"Мережеву гру завершено", L"Network game ended")
STR(kStrConnecting, L"Підключення…", L"Connecting…")
STR(kStrChoosingPlayer, L"Вибір гравця…", L"Choosing a player…")
STR(kStrJoinedCount, L"Приєдналося: %d з %d", L"Joined: %d of %d")
STR(kStrPausedWaiting, L"Гру призупинено: очікуємо гравців",
    L"Paused: waiting for players")
STR(kStrYourTurn, L"Ваш хід!", L"Your turn!")
STR(kStrTurnThinking, L"Хід: %s (думає…)", L"Turn: %s (thinking…)")
STR(kStrTurn, L"Хід: %s", L"Turn: %s")
STR(kStrHintClick, L"ЛКМ — зайняти клітинку", L"Left click — claim a cell")
STR(kStrHintDrag, L"ПКМ + тягнути — рухати поле", L"Right drag — move the board")
STR(kStrHintWheel, L"Колесо / Shift+колесо — прокрутка",
    L"Wheel / Shift+wheel — scroll")
STR(kStrShowLastMove, L"Показувати останній хід", L"Show the last move")

// ---- Cells, players, colours ------------------------------------------------------------
// Ukrainian needs three forms (1 клітинка, 2 клітинки, 5 клітинок), English two.
STR(kStrCellsOne, L"клітинка", L"cell")
STR(kStrCellsFew, L"клітинки", L"cells")
STR(kStrCellsMany, L"клітинок", L"cells")
STR(kStrDefaultPlayer, L"Гравець %d", L"Player %d")
STR(kStrDefaultBot, L"Комп'ютер %d", L"Computer %d")
STR(kStrLevelHuman, L"Людина", L"Human")
STR(kStrLevelWeak, L"Комп'ютер: слабкий", L"Computer: weak")
STR(kStrLevelMedium, L"Комп'ютер: середній", L"Computer: medium")
STR(kStrLevelStrong, L"Комп'ютер: сильний", L"Computer: strong")
STR(kStrLevelVeryStrong, L"Комп'ютер: дуже сильний", L"Computer: very strong")
STR(kStrShortWeak, L"слабкий", L"weak")
STR(kStrShortMedium, L"середній", L"medium")
STR(kStrShortStrong, L"сильний", L"strong")
STR(kStrShortVeryStrong, L"дуже сильний", L"very strong")
STR(kStrColorRed, L"Червоний", L"Red")
STR(kStrColorBlue, L"Синій", L"Blue")
STR(kStrColorGreen, L"Зелений", L"Green")
STR(kStrColorOrange, L"Помаранчевий", L"Orange")
STR(kStrColorPurple, L"Фіолетовий", L"Purple")
STR(kStrColorTurquoise, L"Бірюзовий", L"Turquoise")
STR(kStrColorPink, L"Малиновий", L"Pink")
STR(kStrColorBrown, L"Коричневий", L"Brown")
STR(kStrColorTakenSuffix, L" (зайнятий)", L" (taken)")
STR(kStrOtherColor, L"Інший колір…", L"Other colour…")
STR(kStrColorIsEmpty,
    L"Колір збігається з кольором порожніх клітинок.\nОберіть інший колір.",
    L"This colour is the same as the empty cells.\nChoose another one.")

// ---- Game flow -------------------------------------------------------------------------
STR(kStrResultsTitle, L"Кінець гри", L"Game over")
STR(kStrResultsIntro, L"Гру завершено — усі клітинки поля зафарбовано.\n\n",
    L"The game is over: every cell of the board has been claimed.\n\n")
STR(kStrResultsWinner, L"Переможець: %s (%s %s)\n\n", L"Winner: %s (%s %s)\n\n")
STR(kStrResultsDraw, L"Нічия між гравцями: ", L"A draw between ")
STR(kStrResultsDrawCount, L" (по %s %s)\n\n", L" (%s %s each)\n\n")
STR(kStrResultsSummary, L"Підсумок:\n", L"Final score:\n")
STR(kStrOnlyHostSaves,
    L"Зберегти мережеву гру може лише гравець, який її створив (сервер).",
    L"Only the player who created the network game (the host) can save it.")
STR(kStrNotStartedSave,
    L"Гра ще не почалася: зберегти можна після приєднання всіх гравців.",
    L"The game has not started yet: it can be saved once every player has "
    L"joined.")
STR(kStrSaveErrorTitle, L"Помилка збереження", L"Save failed")
STR(kStrLoadErrorTitle, L"Помилка завантаження", L"Load failed")
STR(kStrConfirmDiscard, L"Поточну гру не збережено. Зберегти її?",
    L"The current game has not been saved. Save it?")
STR(kStrConfirmEndHost,
    L"Мережеву гру буде завершено для всіх гравців. Продовжити?",
    L"The network game will end for every player. Continue?")
STR(kStrConfirmLeave, L"Вийти з мережевої гри?", L"Leave the network game?")
STR(kStrGameAlreadyOver, L"Ця гра вже завершена.", L"This game is already over.")

// ---- Network game --------------------------------------------------------------------
STR(kStrNetErrPortInUse,
    L"Цей порт уже зайнятий іншою програмою. Оберіть інший.",
    L"This port is already used by another program. Choose another one.")
STR(kStrNetErrHostNotFound, L"Не вдалося знайти комп'ютер за цією адресою.",
    L"No computer was found at this address.")
STR(kStrNetErrBadAddress, L"Неправильна адреса або порт.",
    L"The address or the port is not valid.")
STR(kStrNetErrCode, L"Помилка мережі (код %d).", L"Network error (code %d).")
STR(kStrNetCreateFailed, L"Не вдалося створити гру", L"Could not create the game")
STR(kStrNetConnectFailedTitle, L"Не вдалося підключитися", L"Could not connect")
STR(kStrHostSavedTitle, L"Створити мережеву гру зі збереження",
    L"Host a network game from a save")
STR(kStrCreate, L"Створити", L"Create")
STR(kStrHostSavedInfo,
    L"Оберіть свого гравця. Решта гравців під час підключення оберуть серед "
    L"інших кольорів.",
    L"Choose your player. The others will choose among the remaining colours "
    L"when they join.")
STR(kStrJoinTitle, L"Приєднання до гри", L"Joining the game")
STR(kStrJoin, L"Приєднатися", L"Join")
STR(kStrJoinInfo, L"Поле %s × %s, гравців: %d.%s", L"Board %s × %s, players: %d.%s")
STR(kStrJoinColorsSet,
    L"\nКольори гравців уже визначені: оберіть, за кого гратимете.",
    L"\nThe colours are already set: choose which player you will be.")
STR(kStrGameFull, L"У цій грі вже немає вільних місць.",
    L"This game has no free seats left.")
STR(kStrJoinRejected, L"Сервер відхилив запит на приєднання.",
    L"The host turned down the request to join.")
STR(kStrJoinColorTaken, L"Цей колір щойно зайняв інший гравець. Оберіть інший.",
    L"Another player has just taken this colour. Choose another one.")
STR(kStrJoinSlotTaken,
    L"За цього гравця вже грає хтось інший. Оберіть іншого.",
    L"Someone else is already playing as this player. Choose another one.")
STR(kStrNetConnectFailed,
    L"Не вдалося підключитися до гри. Перевірте адресу, порт і те, що гру "
    L"створено (а брандмауер Windows дозволяє з'єднання).",
    L"Could not connect to the game. Check the address and the port, and that "
    L"the game has been created (and that Windows Firewall allows the "
    L"connection).")
STR(kStrNetConnectionLost, L"З'єднання з сервером гри втрачено.",
    L"The connection to the host has been lost.")
STR(kStrNetServerStopped, L"Сервер завершив мережеву гру.",
    L"The host has ended the network game.")
STR(kStrNetVersionMismatch, L"Версія гри на сервері відрізняється від вашої.",
    L"The host runs a different version of the game.")
STR(kStrNetProtocolError, L"Помилка обміну даними з сервером. Гру завершено.",
    L"Data exchange with the host failed. The game has ended.")

// ---- Dialogs ------------------------------------------------------------------------------
STR(kStrCancel, L"Скасувати", L"Cancel")
STR(kStrStartGame, L"Почати гру", L"Start game")
STR(kStrHostTitle, L"Створити мережеву гру", L"Host a network game")
STR(kStrCustomSize, L"Своє", L"Custom")
STR(kStrBoardSizeLabel, L"Розмір поля:", L"Board size:")
STR(kStrWidthHeightLabel, L"Ширина × висота:", L"Width × height:")
STR(kStrPlayerCountLabel, L"Кількість гравців:", L"Number of players:")
STR(kStrPortLabel, L"Порт:", L"Port:")
STR(kStrYouLabel, L"Ви:", L"You:")
STR(kStrBadBoardSize, L"Ширина й висота поля мають бути від 10 до 1 000 000.",
    L"The width and the height of the board must be from 10 to 1,000,000.")
STR(kStrBadPort, L"Порт має бути числом від 1 до 65535.",
    L"The port must be a number from 1 to 65535.")
STR(kStrSameColors,
    L"Гравці %d і %d мають однаковий колір.\nОберіть кожному гравцю інший колір.",
    L"Players %d and %d have the same colour.\nGive each player a different "
    L"colour.")
STR(kStrPlayerColorIsEmpty,
    L"Колір гравця %d збігається з кольором порожніх клітинок.\n"
    L"Оберіть інший колір.",
    L"The colour of player %d is the same as the empty cells.\n"
    L"Choose another one.")
STR(kStrLargeBoard,
    L"Поле %u × %u містить %s клітинок.\n\nПорожні клітинки не займають "
    L"пам'яті, але дуже велика кількість ходів або захоплень може помітно "
    L"навантажити пам'ять і сповільнити гру.\n\nПродовжити?",
    L"A %u × %u board has %s cells.\n\nEmpty cells take no memory, but a very "
    L"large number of moves or captures can use a lot of it and slow the game "
    L"down.\n\nContinue?")
STR(kStrLargeBoardTitle, L"Велике поле", L"Large board")
STR(kStrConnectTitle, L"Приєднатися до мережевої гри", L"Join a network game")
STR(kStrConnectInfo,
    L"Адреса комп'ютера, що створив гру (наприклад, 192.168.1.10):",
    L"Address of the computer hosting the game (e.g. 192.168.1.10):")
STR(kStrIpAddressLabel, L"IP-адреса:", L"IP address:")
STR(kStrEnterAddress, L"Введіть IP-адресу комп'ютера, що створив гру.",
    L"Enter the IP address of the computer hosting the game.")
STR(kStrChooseWhom, L"Оберіть, за кого грати:", L"Choose your player:")
STR(kStrYourName, L"Ваше ім'я:", L"Your name:")
STR(kStrColorLabel, L"Колір:", L"Colour:")
STR(kStrPickFromList, L"Оберіть гравця зі списку.",
    L"Choose a player from the list.")
STR(kStrColorTakenByOther,
    L"Цей колір уже зайнятий іншим гравцем.\nОберіть інший колір.",
    L"This colour is already taken by another player.\nChoose another one.")

// ---- Saves ---------------------------------------------------------------------------------
STR(kStrFilterSaves, L"Збереження Feodals (*.feo)", L"Feodals saves (*.feo)")
STR(kStrFilterAll, L"Усі файли (*.*)", L"All files (*.*)")
STR(kStrSaveOk, L"Готово.", L"Done.")
STR(kStrSaveErrOpen, L"Не вдалося відкрити файл.", L"Could not open the file.")
STR(kStrSaveErrWrite, L"Не вдалося записати файл.", L"Could not write the file.")
STR(kStrSaveErrRead, L"Помилка читання файлу.", L"Could not read the file.")
STR(kStrSaveErrFormat, L"Файл пошкоджено або це не збереження Feodals.",
    L"The file is damaged or is not a Feodals save.")
STR(kStrSaveErrVersion, L"Непідтримувана версія формату збереження.",
    L"This version of the save format is not supported.")
STR(kStrSaveErrMemory, L"Недостатньо пам'яті для завантаження гри.",
    L"Not enough memory to load the game.")
STR(kStrSaveErrUnknown, L"Невідома помилка.", L"Unknown error.")

// ---- Help --------------------------------------------------------------------------------
STR(kStrRulesTitle, L"Правила гри", L"Rules")
STR(kStrRules,
    L"Гравці ходять по черзі. Хід — клік лівою кнопкою миші по нічиїй "
    L"клітинці: вона фарбується у ваш колір.\n\n"
    L"Захоплення оточенням: якщо ваші клітинки повністю оточують область "
    L"(нічиї та/або чужі клітинки), уся вона переходить до вас.\n"
    L"• Як у «Крапках» на папері, клітинки, що торкаються лише кутами, "
    L"теж утворюють суцільну стіну.\n"
    L"• Край поля не є стіною: область, що торкається краю, не "
    L"захоплюється.\n\n"
    L"Гра закінчується, коли всі клітинки зафарбовано. Перемагає гравець "
    L"з найбільшою кількістю клітинок.\n\n"
    L"Керування полем:\n"
    L"• колесо миші — вертикальна прокрутка, Shift+колесо — горизонтальна;\n"
    L"• права кнопка миші (затиснути й тягнути) — перетягування поля;\n"
    L"• стрілки, PageUp/PageDown, Home/End — прокрутка клавіатурою.",
    L"Players take turns. A move is a left click on an unclaimed cell: it "
    L"takes your colour.\n\n"
    L"Capture by encirclement: when your cells completely surround an area "
    L"(unclaimed and/or other players' cells), all of it becomes yours.\n"
    L"• As with dots on paper, cells that touch only at their corners also "
    L"form a solid wall.\n"
    L"• The edge of the board is not a wall: an area that touches the edge "
    L"is not captured.\n\n"
    L"The game ends when every cell has been claimed. The player with the "
    L"most cells wins.\n\n"
    L"Moving around the board:\n"
    L"• mouse wheel — scroll up and down, Shift+wheel — left and right;\n"
    L"• right mouse button (hold and drag) — drag the board;\n"
    L"• arrow keys, PageUp/PageDown, Home/End — scroll with the keyboard.")
STR(kStrAboutTitle, L"Про програму", L"About Feodals")
STR(kStrAbout,
    L"Feodals %s\n"
    L"Дата збірки: %s\n"
    L"Автор: %s\n\n"
    L"Покрокова гра на захоплення території для 2–8 гравців на одному "
    L"комп'ютері або по локальній мережі.\n"
    L"C++ і чистий Win32 API, без сторонніх бібліотек.",
    L"Feodals %s\n"
    L"Built on: %s\n"
    L"Author: %s\n\n"
    L"A turn-based territory capture game for 2–8 players on one computer or "
    L"over a local network.\n"
    L"C++ and the plain Win32 API, no third-party libraries.")
STR(kStrAuthor, L"Павло Галковський", L"Pavlo Halkovskyi")
