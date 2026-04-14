# Terminal-Friendly OS Task Backlog 

Легенда статусов:

* `[x]` done
* `[~]` partial
* `[ ]` not implemented
* `[!]` unstable
* `[t]` needs testing
* `[d]` documented

Легенда приоритетов:

* `P0` — критический фундамент
* `P1` — ядро UX
* `P2` — важные улучшения
* `P3` — опционально

---

## 1) Boot & Recovery

* `[x]` `BOOT-001 (P0)` Меню загрузки отображается
* `[x]` `BOOT-002 (P0)` Есть пункт recovery shell
* `[x]` `BOOT-003 (P0)` Логи последней загрузки сохраняются
* `[x]` `BOOT-004 (P0)` Логи читаются из recovery
* `[x]` `BOOT-005 (P1)` Ошибка загрузки показывает причину
* `[x]` `BOOT-006 (P1)` Ошибка предлагает действия
* `[x]` `BOOT-007 (P1)` Можно выбрать прошлый kernel

## 2) Filesystem Layout

* `[x]` `FSUX-001 (P0)` /home отделён от system
* `[x]` `FSUX-002 (P0)` runtime (tmp/run) отделён
* `[x]` `FSUX-003 (P0)` логи в одном месте
* `[x]` `FSUX-004 (P1)` команда объясняет каталог
* `[x]` `FSUX-005 (P1)` видно mount points
* `[x]` `FSUX-006 (P2)` видно источник файла (package)

## 3) Shell

* `[x]` `SH-001 (P0)` История команд сохраняется
* `[x]` `SH-002 (P0)` Поиск по истории работает
* `[x]` `SH-003 (P0)` Tab completion
* `[x]` `SH-004 (P1)` Prompt показывает cwd
* `[x]` `SH-005 (P1)` Prompt показывает exit code
* `[x]` `SH-006 (P1)` Prompt не ломает скрипты
* `[ ]` `SH-007 (P2)` Git статус в prompt

## 4) Errors

* `[x]` `ERR-001 (P0)` Ошибка указывает объект (файл/сервис)
* `[x]` `ERR-002 (P0)` Ошибка указывает причину
* `[x]` `ERR-003 (P0)` Ошибка предлагает действие
* `[x]` `ERR-004 (P1)` Разные коды ошибок
* `[x]` `ERR-005 (P1)` verbose режим

## 5) Package Manager

* `[x]` `PKG-001 (P0)` install работает
* `[x]` `PKG-002 (P0)` remove работает
* `[x]` `PKG-003 (P0)` search работает
* `[x]` `PKG-004 (P0)` update работает
* `[x]` `PKG-005 (P0)` список изменений ДО применения
* `[x]` `PKG-006 (P1)` dry-run
* `[x]` `PKG-007 (P1)` downgrade
* `[x]` `PKG-008 (P1)` проверка хэшей

## 6) Docs

* `[x]` `DOC-001 (P0)` man page существует
* `[x]` `DOC-002 (P0)` есть примеры
* `[x]` `DOC-003 (P1)` команда help
* `[x]` `DOC-004 (P1)` troubleshooting есть

## 7) Services

* `[x]` `SRV-001 (P0)` start
* `[x]` `SRV-002 (P0)` stop
* `[x]` `SRV-003 (P0)` status
* `[x]` `SRV-004 (P1)` enable
* `[x]` `SRV-005 (P1)` disable
* `[x]` `SRV-006 (P1)` статус показывает ошибки

## 8) Logging

* `[x]` `LOG-001 (P0)` логи доступны
* `[x]` `LOG-002 (P0)` фильтр по времени
* `[x]` `LOG-003 (P1)` фильтр по сервису
* `[x]` `LOG-004 (P1)` экспорт логов

## 9) Permissions

* `[x]` `SEC-001 (P0)` chmod работает
* `[x]` `SEC-002 (P0)` chown работает
* `[x]` `SEC-003 (P0)` ошибка доступа объясняется
* `[x]` `SEC-004 (P1)` sudo есть

## 10) Terminal

* `[x]` `TERM-001 (P0)` UTF-8 корректен
* `[x]` `TERM-002 (P0)` цвета работают
* `[x]` `TERM-003 (P0)` pager листает
* `[x]` `TERM-004 (P0)` pager выходит без зависаний
* `[x]` `TERM-005 (P1)` pager поиск
* `[x]` `TERM-006 (P1)` pager интеграция

## 11) Editor

* `[x]` `EDIT-001 (P0)` открыть файл
* `[x]` `EDIT-002 (P0)` сохранить файл
* `[x]` `EDIT-003 (P1)` поиск текста
* `[x]` `EDIT-004 (P1)` подсветка синтаксиса

## 12) Search

* `[x]` `FIND-001 (P0)` find файлы
* `[x]` `FIND-002 (P0)` grep текст
* `[x]` `FIND-003 (P1)` поиск команд

## 13) Networking

* `[x]` `NET-001 (P0)` показать IP
* `[x]` `NET-002 (P0)` показать route
* `[x]` `NET-003 (P0)` поддержка разных сетевых контролерров (рабочие: e1000/e1000e/часть igb + RTL8139 + RTL8168/8111 + virtio-net legacy, матрица в `docs/NETWORK_DRIVERS.md`)
* `[x]` `NET-004 (P1)` ping работает
* `[x]` `NET-005 (P1)` SSH работает

## 14) Processes

* `[x]` `PROC-001 (P0)` список процессов
* `[x]` `PROC-002 (P0)` kill работает
* `[x]` `PROC-003 (P1)` CPU usage
* `[x]` `PROC-004 (P1)` tree

## 15) Storage

* `[x]` `DISK-001 (P0)` df
* `[x]` `DISK-002 (P0)` du
* `[x]` `DISK-003 (P1)` проверка диска

## 16) Backup

* `[x]` `BKP-001 (P0)` backup создать
* `[x]` `BKP-002 (P1)` restore

## 17) Resources

* `[x]` `RES-001 (P0)` CPU usage
* `[x]` `RES-002 (P0)` RAM usage

## 18) Scripting

* `[x]` `SCRIPT-001 (P0)` exit codes
* `[x]` `SCRIPT-002 (P1)` стабильный CLI

## 19) Dev

* `[x]` `DEV-001 (P1)` компилятор
* `[x]` `DEV-002 (P1)` debugger

## 20) Philosophy

* `[x]` `PHIL-001 (P0)` нет скрытых действий
* `[x]` `PHIL-002 (P0)` ошибки не игнорируются
* `[x]` `PHIL-003 (P0)` не является Unix-based 

---

## Meta

* `[x]` `META-001 (P0)` Можно ответить: что это
* `[x]` `META-002 (P0)` Где настраивается
* `[x]` `META-003 (P0)` Как дебажить
