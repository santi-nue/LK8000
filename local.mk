ifeq ($(CONFIG_PC),y) 
 TCPATH :=i686-w64-mingw32-
 CPU    :=i586
 MCPU   := -mcpu=$(CPU)
endif
