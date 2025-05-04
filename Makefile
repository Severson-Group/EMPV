all:
	gcc empv.c -L./Linux -lglfw3 -ldl -lm -lX11 -lglad -lGL -lGLU -lpthread -DOS_LINUX -o empv.o
rel:
	gcc empv.c -L./Linux -lglfw3 -ldl -lm -lX11 -lglad -lGL -lGLU -lpthread -DOS_LINUX -O3 -o empv.o
win:
	gcc empv.c -L./Windows -lglfw3 -lopengl32 -lgdi32 -lglad -lole32 -luuid -lwsock32 -lWs2_32 -DOS_WINDOWS -DDEBUGGING_FLAG -o empv.exe
winrel:
	gcc empv.c -L./Windows -lglfw3 -lopengl32 -lgdi32 -lglad -lole32 -luuid -lwsock32 -lWs2_32 -DOS_WINDOWS -O3 -o empv.exe
tcp:
	gcc testTCP.c -lwsock32 -lWs2_32 -o testTCP.exe
fft:
	gcc include/fft.c -o fft.exe