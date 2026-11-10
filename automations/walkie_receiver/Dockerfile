ARG BUILD_FROM
FROM $BUILD_FROM

ENV HOME=/root

# Только готовые бинарные пакеты Alpine - ничего не компилируется,
# сборка занимает секунды даже на слабом ARM (Raspberry Pi 3).
RUN apk add --no-cache \
        python3 \
        pulseaudio-utils \
        mosquitto-clients

COPY run.sh /
COPY walkie_receiver.py /
RUN chmod a+x /run.sh

CMD [ "/run.sh" ]
