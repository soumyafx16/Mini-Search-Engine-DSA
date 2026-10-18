@echo off
gcc gui.c web.c index.c -o mini_search.exe -mwindows -lwininet -lgdiplus -lole32 -lgdi32 -luser32 -lshell32 -lm
if errorlevel 1 (
    echo.
    echo BUILD FAILED - copy the errors above and send them back
    pause
) else (
    echo Build OK - starting...
    start mini_search.exe
)