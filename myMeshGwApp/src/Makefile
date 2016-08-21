
C_FILES = ./application/mySniffer.c

CFLAGS = -Werror \
        -Wunused \
        -Wuninitialized \
        -Wreturn-type \
        -Wmissing-prototypes \
        -Wmissing-declarations \
        -Wunreachable-code \
        -Wreturn-type

INCLUDES = -I ./../csrMeshGwRefApp/meshSocket \
	-I ./../csrMeshGwRefApp/utils \
	-I ./../csrMeshGwRefApp/include \
	-I ./../csrMeshGwRefApp/serializer/csr-model-gwnb/src/autogen \
	-I ./../csrMeshGwRefApp/serializer/csr-model-apis-inc/include \
	-I ./../csrMeshGwRefApp/serializer/csr-model-apis-inc/apis \
	-I ./../csrMeshGwRefApp/serializer/csr-core-apis
	
LIB_FILES = -lgwMeshSocket \
	-lgwSerializer \
	-lgwUtils

LIB_PATH = -L ./../csrMeshGwRefApp/serializer \
	-L ./../csrMeshGwRefApp/meshSocket \
	-L ./../csrMeshGwRefApp/utils

ifdef OPENWRT_BUILD
CFLAGS += -DARCH_BIG_ENDIAN
endif

OBJ_FILES=$(patsubst %.c, %.o, $(C_FILES))

%.o:%.c 
	$(CC) $(INCLUDES) $(CFLAGS) -c $*.c -o $*.o

myMeshGwApp: $(OBJ_FILES)
	$(CC) $(LDFLAGS) $(INCLUDES) $(LIB_PATH) $(OBJ_FILES) -o myMeshGwApp $(LIB_FILES)

# remove object files and executable when user executes "make clean"
clean:
	rm *.o 

