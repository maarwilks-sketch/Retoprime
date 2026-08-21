@echo off
setlocal
py -m pip install -r requirements-build.txt
if errorlevel 1 exit /b 1
py -m unittest discover -s tests -v
if errorlevel 1 exit /b 1
py -m PyInstaller --noconfirm --clean RETOPRIME.spec
if errorlevel 1 exit /b 1
echo Built: dist\RETOPRIME.exe
