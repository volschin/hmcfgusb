# HMCFGUSB
# http://git.zerfleddert.de/cgi-bin/gitweb.cgi/hmcfgusb
FROM alpine:3.23

# Package version
ARG HMCFGUSB_VER=0.104

COPY * /app/hmcfgusb
WORKDIR /app/hmcfgusb

# Install build packages
RUN apk add --no-cache --virtual=build-dependencies \
            build-base \
            clang cmake ccache \
            libusb-dev \
# Install runtime packages
 && apk add --no-cache --update \
            libusb \
            ca-certificates \
# Install app
 && cd /app/hmcfgusb \
 && clang --version && make \
# Cleanup
 && apk del --purge build-dependencies \
 && rm *.h *.o *.c *.d
 
EXPOSE 1234

CMD ["/app/hmcfgusb/hmland", "-v", "-p 1234", "-I"]
