@echo off
setlocal
cl /nologo /std:c11 /O2 /W4 myafs_driver.c main.c /Fe:myafsdrv.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c11 /O2 /W4 myafs_driver.c mkfs_myafs.c /Fe:mkfs.myafs.exe
if errorlevel 1 exit /b %errorlevel%

echo built myafsdrv.exe
echo built mkfs.myafs.exe
