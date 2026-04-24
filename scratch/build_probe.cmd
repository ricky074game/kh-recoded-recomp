@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /EHsc ^
  /I "C:\Users\Ricky\Desktop\Codes\Cool Coding\kh-recoded-recomp\build\_deps\capstone-src\include" ^
  "C:\Users\Ricky\Desktop\Codes\Cool Coding\kh-recoded-recomp\scratch\disasm_probe.cpp" ^
  /link ^
  /OUT:"C:\Users\Ricky\Desktop\Codes\Cool Coding\kh-recoded-recomp\scratch\disasm_probe.exe" ^
  "C:\Users\Ricky\Desktop\Codes\Cool Coding\kh-recoded-recomp\build\_deps\capstone-build\Release\capstone.lib"
