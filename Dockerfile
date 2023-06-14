# HMCFGUSB
# http://git.zerfleddert.de/cgi-bin/gitweb.cgi/hmcfgusb
FROM alpine:3.18.2

# Package version
ARG HMCFGUSB_VER=0.103

COPY * /app/hmcfgusb
WORKDIR /app/hmcfgusb

# Install build packages
RUN apk add --no-cache --virtual=build-dependencies \
            build-base \
            libusb-dev \
# Install runtime packages
 && apk add --no-cache \
            libusb \
# Install app
 && cd /app/hmcfgusb \
 && make \
# Cleanup
 && apk del --purge build-dependencies \
 && rm *.h *.o *.c *.d
 
EXPOSE 1234

CMD ["/app/hmcfgusb/hmland", "-v", "-p 1234", "-I"]
