#!/usr/bin/with-contenv bashio

export MQTT_HOST=$(bashio::config 'mqtt_host')
export MQTT_PORT=$(bashio::config 'mqtt_port')
export MQTT_USER=$(bashio::config 'mqtt_user')
export MQTT_PASSWORD=$(bashio::config 'mqtt_password')
export SAMPLE_RATE=$(bashio::config 'sample_rate')

bashio::log.info "Запуск приёмника рации (MQTT: ${MQTT_HOST}:${MQTT_PORT}, sample_rate: ${SAMPLE_RATE})"

# exec, чтобы python получал сигналы напрямую (корректное завершение/watchdog)
exec python3 /walkie_receiver.py
