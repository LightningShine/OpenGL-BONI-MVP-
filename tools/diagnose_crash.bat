@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
title RAJAGP Diagnostics

set "LOG=%USERPROFILE%\Desktop\rajagp_log.txt"
echo RAJAGP diagnostics > "%LOG%"
echo ============================== >> "%LOG%"

REM --- ищем OpenGL.exe в типичных местах установки ---
set "EXE="
for %%P in (
  "%LocalAppData%\Programs\RajaSoftware\OpenGL.exe"
  "%ProgramFiles%\RajaSoftware\OpenGL.exe"
  "%ProgramFiles(x86)%\RajaSoftware\OpenGL.exe"
  "%LocalAppData%\Programs\BONI\OpenGL.exe"
  "%ProgramFiles%\BONI\OpenGL.exe"
) do if exist "%%~P" set "EXE=%%~P"

if not defined EXE (
  echo OpenGL.exe НЕ найден в стандартных папках. >> "%LOG%"
  echo Найди папку, куда установилась программа, положи этот файл туда и запусти снова. >> "%LOG%"
  goto show
)

echo Найдено: %EXE% >> "%LOG%"
for %%D in ("%EXE%") do set "DIR=%%~dpD"
echo Рабочая папка: %DIR% >> "%LOG%"
echo ============================== >> "%LOG%"
echo --- ВЫВОД ПРИЛОЖЕНИЯ --- >> "%LOG%"

REM --- запускаем из его папки, весь вывод -> в лог ---
pushd "%DIR%"
"%EXE%" >> "%LOG%" 2>&1
set "CODE=%ERRORLEVEL%"
popd

echo. >> "%LOG%"
echo ============================== >> "%LOG%"
echo Код завершения: %CODE% >> "%LOG%"
echo (0xC0000005 = краш памяти, 0xC000007B = неверная архитектура DLL) >> "%LOG%"

REM --- последняя ошибка из журнала Windows ---
echo. >> "%LOG%"
echo --- ПОСЛЕДНЯЯ ОШИБКА ИЗ ЖУРНАЛА WINDOWS --- >> "%LOG%"
powershell -NoProfile -Command "try { (Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'} -MaxEvents 1).Message } catch { 'нет записей Application Error' }" >> "%LOG%" 2>&1

:show
echo Готово. Лог: %LOG%
start "" notepad "%LOG%"
endlocal
