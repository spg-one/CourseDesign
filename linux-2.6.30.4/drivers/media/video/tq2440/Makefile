TQ2440_CAMERA_SRC = $(shell ls drivers/media/video/tq2440/cstm.c 2>/dev/null)

ifeq ($(TQ2440_CAMERA_SRC),)
obj-$(CONFIG_TQ2440_camera)			+= tq2440_camera.module
obj-$(CONFIG_TQ2440_camera)			+= tq2440_camera.module
else
tq2440_camera-objs					:= cstm.o ov9650_fun.o saa7113_fun.o
obj-$(CONFIG_TQ2440_camera)			+= tq2440_camera.o
endif

obj-$(CONFIG_VIDEO_OV9650)			+= ov9650.o

