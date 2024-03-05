set files=main.cpp
set libs=lib\SDL2main.lib lib\SDL2.lib

CL /Zi /I include %files% /link %libs% /OUT:main.exe