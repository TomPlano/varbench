SRC = socketcorpus.c
OBJ = socketcorpus.o
TARGET = socketcorpus.so
all: $(TARGET)



$(TARGET): $(OBJ)
	@echo "	$(CC) -O0 -shared -pthread -o $(TARGET) $(OBJ)"
	@$(CC) -O0 -shared -pthread -o $(TARGET) $(OBJ)

$(OBJ): $(SRC)
	@echo "	$(CC) -O0 -fpic -c -pthread $(SRC)"
	@$(CC) -O0 -fpic -c -pthread $(SRC)

veryclean:
	@echo "	$(RM) $(TARGET) $(OBJ)"
	@$(RM) $(TARGET) $(OBJ)
