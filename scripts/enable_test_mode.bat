@echo off
echo включаю test signing
bcdedit /set testsigning on
echo если secure boot включен, windows может не дать включить test mode.
echo после команды перезагрузи компьютер.
pause
