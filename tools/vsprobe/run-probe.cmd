@echo off
rem TL4 reference probe inside Windows Sandbox. Reads the stock aero.msstyles of the sandbox's own Windows (the host
rem build), asks uxtheme.dll the queries of the list, writes CSV to the writable output folder. Changes nothing.
setlocal
set IN=C:\probe\in
set OUT=C:\probe\out
set STYLE=C:\Windows\Resources\Themes\aero\aero.msstyles

certutil -hashfile "%IN%\vsprobe.exe" SHA256 > "%OUT%\vsprobe.exe.sha256.txt"
certutil -hashfile "%STYLE%" SHA256 > "%OUT%\aero.msstyles.sha256.txt"
ver > "%OUT%\windows-ver.txt"
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v DisplayVersion >> "%OUT%\windows-ver.txt" 2>nul
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v UBR >> "%OUT%\windows-ver.txt" 2>nul

"%IN%\vsprobe.exe" "%STYLE%" "%IN%\queries-aero.csv" "%OUT%\aero-26100-reference.csv" 2> "%OUT%\vsprobe-stderr.txt"
echo exit code %ERRORLEVEL% >> "%OUT%\vsprobe-stderr.txt"
echo done > "%OUT%\DONE"
endlocal
