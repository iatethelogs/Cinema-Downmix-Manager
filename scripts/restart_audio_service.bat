@echo off
echo перезапуск windows audio
net stop audiosrv /y
net start audiosrv
pause
