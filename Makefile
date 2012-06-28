LIBS=gstreamer-0.10 gstreamer-app-0.10 glib-2.0 libxml-2.0
CFLAGS+=$(shell pkg-config --cflags $(LIBS))
LDFLAGS+=$(shell pkg-config --libs $(LIBS)) -lz
CFLAGS+=-Wall
#CFLAGS+=-g

APP=gst-httpd
OBJS=http-server.o http-client.o media-mapping.o media.o rate.o v4l2-ctl.o main.o
DEPS=http-client.h http-server.h media-mapping.h media.h rate.h v4l2-ctl.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(APP)

clean:
	rm -f $(APP) *.o
