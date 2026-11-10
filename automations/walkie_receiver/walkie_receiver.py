#!/usr/bin/env python3
"""
Приёмник звука для рации - надёжная версия без pip-зависимостей.

Протокол максимально простой: ESP32 подключается по обычному TCP-сокету
(без HTTP/WebSocket) и сразу шлёт сырой 16-битный PCM. Никакого заголовка,
никакого парсинга - просто поток байт, который мы пишем в paplay.

MQTT реализован через консольные mosquitto_pub/mosquitto_sub (пакет
mosquitto-clients) вместо библиотеки paho-mqtt - это готовый бинарник,
не требующий компиляции при сборке образа.

Устойчивость к сбоям:
  - если paplay падает во время сеанса - перезапускаем его на лету
  - если mosquitto_sub (слушатель MQTT) обрывается - перезапускаем его
  - если TCP-соединение обрывается - просто ждём следующее, ничего не падает
  - любое необработанное исключение в одной "задаче" не должно ронять
    остальные части программы
"""

import asyncio
import os
import subprocess
import logging

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("walkie")

# ============ НАСТРОЙКИ (от аддона через переменные окружения) ============
TCP_HOST = "0.0.0.0"
TCP_PORT = 8765

SAMPLE_RATE = os.environ.get("SAMPLE_RATE", "16000")  # ДОЛЖНО совпадать с ESP32
CHANNELS = "1"

MQTT_HOST = os.environ.get("MQTT_HOST", "core-mosquitto")
MQTT_PORT = os.environ.get("MQTT_PORT", "1883")
MQTT_USER = os.environ.get("MQTT_USER", "")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "")

TOPIC_RPI_STATUS   = "walkie/rpi_status"
TOPIC_TALKING      = "walkie/talking"
TOPIC_VOLUME_SET   = "walkie/output_volume/set"
TOPIC_VOLUME_STATE = "walkie/output_volume/state"


def mosquitto_auth_args():
    args = ["-h", MQTT_HOST, "-p", MQTT_PORT]
    if MQTT_USER:
        args += ["-u", MQTT_USER, "-P", MQTT_PASSWORD]
    return args


def mqtt_publish(topic: str, payload: str, retain: bool = False):
    args = ["mosquitto_pub"] + mosquitto_auth_args() + ["-t", topic, "-m", payload]
    if retain:
        args.append("-r")
    try:
        subprocess.run(args, timeout=5, capture_output=True)
    except Exception as e:
        log.warning("Не удалось опубликовать MQTT (%s): %s", topic, e)


def set_volume(percent: int):
    try:
        subprocess.run(
            ["pactl", "set-sink-volume", "@DEFAULT_SINK@", f"{percent}%"],
            check=True, timeout=5, capture_output=True,
        )
        log.info("Громкость рации: %d%%", percent)
        mqtt_publish(TOPIC_VOLUME_STATE, str(percent), retain=True)
    except Exception as e:
        log.error("Не удалось установить громкость: %s", e)


# ============ MQTT: СЛУШАТЕЛЬ КОМАНД (с автопереподключением) ============
async def mqtt_listener_loop():
    """
    Держит mosquitto_sub подписанным на топик громкости. Если процесс
    падает (сеть/брокер моргнули) - просто перезапускаем через паузу,
    вечно, без падения программы.
    """
    while True:
        try:
            mqtt_publish(TOPIC_RPI_STATUS, "online", retain=True)

            args = ["mosquitto_sub"] + mosquitto_auth_args() + ["-t", TOPIC_VOLUME_SET]
            proc = await asyncio.create_subprocess_exec(
                *args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
            )
            log.info("MQTT-слушатель запущен")

            while True:
                line = await proc.stdout.readline()
                if not line:
                    break  # процесс завершился - выходим во внешний retry-цикл
                try:
                    volume = int(line.decode().strip())
                    volume = max(0, min(100, volume))
                    set_volume(volume)
                except ValueError:
                    log.warning("Некорректная громкость в MQTT: %r", line)

            log.warning("MQTT-слушатель оборвался, перезапуск через 5с")
        except Exception as e:
            log.error("Ошибка MQTT-слушателя: %s, перезапуск через 5с", e)

        await asyncio.sleep(5)


# ============ ВОСПРОИЗВЕДЕНИЕ ЗВУКА ЧЕРЕЗ PULSEAUDIO ============
def start_paplay() -> subprocess.Popen:
    cmd = ["paplay", "--raw"]
    pulse_server = os.environ.get("PULSE_SERVER")
    if pulse_server:
        cmd.append(f"--server={pulse_server}")
    cmd += [
        f"--rate={SAMPLE_RATE}",
        f"--channels={CHANNELS}",
        "--format=s16le",
        # Без аргумента FILE: эта сборка paplay (Alpine/musl) не понимает "-"
        # как условное обозначение stdin и пытается open("-") буквально
        # (отсюда "open(): No such file or directory" в логах). Без FILE
        # paplay по умолчанию и так читает из stdin - это задокументированное
        # и переносимое поведение.
    ]
    return subprocess.Popen(cmd, stdin=subprocess.PIPE)


def _reap(proc: subprocess.Popen):
    """Забрать код завершения уже мёртвого процесса, чтобы не копить зомби."""
    try:
        proc.wait(timeout=0)
    except Exception:
        pass


async def handle_connection(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    peer = writer.get_extra_info("peername")
    log.info("ESP32 подключился: %s", peer)
    mqtt_publish(TOPIC_TALKING, "ON")

    proc = start_paplay()

    try:
        while True:
            data = await reader.read(4096)
            if not data:
                break  # ESP32 закрыл соединение (кнопку отпустили)

            # Если paplay неожиданно умер во время сеанса - поднимаем заново,
            # чтобы не терять оставшуюся часть разговора.
            if proc.poll() is not None:
                log.warning("paplay неожиданно завершился, перезапускаю")
                _reap(proc)
                proc = start_paplay()

            try:
                proc.stdin.write(data)
                proc.stdin.flush()
            except BrokenPipeError:
                log.warning("paplay недоступен, перезапускаю")
                _reap(proc)
                proc = start_paplay()
    except Exception as e:
        log.error("Ошибка при приёме звука: %s", e)
    finally:
        log.info("ESP32 отключился: %s", peer)
        mqtt_publish(TOPIC_TALKING, "OFF")
        try:
            proc.stdin.close()  # сигнал paplay "данных больше не будет"
        except Exception:
            pass

        # Не убиваем сразу - даём paplay доиграть то, что уже успело
        # попасть в буфер (иначе обрезается последнее слово из-за задержки
        # конвейера). Ждём естественного завершения до 8 секунд, и только
        # если он завис - глушим принудительно.
        DRAIN_TIMEOUT = 8.0
        waited = 0.0
        while proc.poll() is None and waited < DRAIN_TIMEOUT:
            await asyncio.sleep(0.1)
            waited += 0.1

        if proc.poll() is None:
            log.warning("paplay не доиграл за %.0fс, останавливаю принудительно", DRAIN_TIMEOUT)
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except Exception:
                proc.kill()
        else:
            log.info("paplay доиграл буфер и завершился сам (%.1fс)", waited)

        writer.close()


async def tcp_server_loop():
    server = await asyncio.start_server(handle_connection, TCP_HOST, TCP_PORT)
    log.info("TCP-сервер слушает %s:%d", TCP_HOST, TCP_PORT)
    async with server:
        await server.serve_forever()


async def main():
    # Обе задачи работают параллельно и независимо: если одна споткнётся,
    # исключение не должно тихо убить вторую.
    await asyncio.gather(
        tcp_server_loop(),
        mqtt_listener_loop(),
    )


if __name__ == "__main__":
    asyncio.run(main())
