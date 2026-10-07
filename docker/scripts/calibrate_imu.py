"""Interactive manual BNO calibration over Wi-Fi, with explicit operator actions."""
import argparse
import json
import math
import os
from pathlib import Path
import select
import shutil
import socket
import sys
import time

from status import LineReader, ProtocolError, fields, integer, ipv4, port_number, timeout_seconds, utc_now

INSTRUCTIONS = {
    "IDLE": "Digite iniciar (ou iniciar 4/5/6) para começar; sair para fechar o monitor.",
    "PREPARING": "Preparando calibração manual e relatórios. Aguarde; nenhum movimento foi confirmado.",
    "ENVIRONMENT": "Afaste a trena montada de estruturas magnéticas, PC e monitores. Enter confirma o ambiente.",
    "OBSERVE_MAG": "Observe o status Magnetic Field abaixo. Enter confirma que está acompanhando o indicador recente.",
    "ACCEL_POSITION": "Oriente em uma posição distinta e mantenha por ~1 s. Sugestão: faces de um cubo, em qualquer ordem. Enter confirma esta posição.",
    "GYRO_REST": "Apoie em superfície imóvel por ~2–3 s. Enter confirma que realizou o repouso.",
    "MAG_ROLL": "Gire ~180° e volte ao início em roll, em ~2 s. Enter confirma este eixo.",
    "MAG_PITCH": "Gire ~180° e volte ao início em pitch, em ~2 s. Enter confirma este eixo.",
    "MAG_YAW": "Gire ~180° e volte ao início em yaw, em ~2 s. Enter confirma este eixo.",
    "SAVE_DCD": "Movimentos confirmados. Com Magnetic Field recente 2 ou 3, digite salvar para gravar DCD.",
    "WAIT_SAVE": "Gravação enviada. Aguardando confirmação do sensor; não reenviar. Cancelar não desfaz o envio.",
    "RESTORING": "Restaurando máscara original e Rotation Vector a 50 Hz; auxiliares serão desabilitados.",
}
QUALITIES = {"-1": "ausente", "0": "não confiável", "1": "baixo", "2": "médio", "3": "alto"}


class Journal:
    def __init__(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.file = path.open("a", encoding="utf-8")

    def write(self, event, **values):
        self.file.write(json.dumps({"pc_utc": utc_now(), "event": event, **values}, ensure_ascii=False) + "\n")
        self.file.flush()

    def close(self):
        self.file.close()


class Monitor:
    def __init__(self, sock, timeout, journal):
        self.sock, self.timeout, self.journal = sock, timeout, journal
        self.reader = LineReader(sock, timeout, max_line_bytes=2048)
        self.owned = None
        self.current = None
        self.save_attempted = False
        self.awaiting_reply = None

    def handshake(self):
        hello = self.reader.read("HELLO")
        h = fields(hello, "# HELLO MM1LAB 2", ("stage", "boot_id", "connection_id"))
        if h["stage"] != "unit_capture":
            raise ProtocolError("Firmware não oferece a etapa de sensores")
        meta = self.reader.read("META")
        m = fields(meta, "# META", ("commands", "cal_schema", "cal_line_max"))
        if "CAL_IMU" not in m["commands"].split(",") or m["cal_schema"] != "1" or m["cal_line_max"] != "2048":
            raise ProtocolError("Contrato CAL_IMU não suportado")
        self.journal.write("handshake", hello=hello, meta=meta)

    def exchange(self, command):
        self.journal.write("command", command=command)
        if command.startswith("CAL_IMU SAVE "):
            self.save_attempted = True # Lost response must never cause an implicit retry.
        self.sock.settimeout(self.timeout)
        self.awaiting_reply = command
        self.sock.sendall((command + "\n").encode("ascii"))
        return self.receive_reply(command)

    def receive_reply(self, command):
        line = self.reader.read(command)
        self.journal.write("response", command=command, line=line)
        prefix = "# OK CAL_IMU" if line.startswith("# OK CAL_IMU ") else "# ERR CAL_IMU"
        values = fields(line, prefix, (
            "action", "code", "session_id", "state", "phase", "step_id", "stage", "instruction", "confirmed",
            "positions", "position", "round", "owner", "allowed", "result", "detail", "dcd", "restore",
            "remaining_ms", "limit_ms", "fresh_ms", "op_ms", "rc", "original_mask", "effective_mask",
            "autosave_rc", "autosave_ack", "rv_us", "game_us", "mag_us", "accuracy_rad",
            *(f"{sensor}_{key}" for sensor in ("mag", "game", "rv") for key in ("status", "age_ms", "gen", "seq")),
        ))
        if values["action"] != command.split()[1]:
            raise ProtocolError("ACK de outro comando")
        for key in ("session_id", "step_id", "confirmed", "remaining_ms"):
            integer(values, key, 0, 0xFFFFFFFF)
        for key, low, high in (("stage", 0, 6), ("positions", 4, 6), ("position", 0, 6),
                               ("op_ms", 100, 5000), ("limit_ms", 1000, 1800000), ("fresh_ms", 100, 2000),
                               ("original_mask", -1, 31), ("effective_mask", 0, 31)):
            integer(values, key, low, high)
        for sensor in ("mag", "game", "rv"):
            integer(values, f"{sensor}_status", -1, 3)
            integer(values, f"{sensor}_age_ms", -1, 0xFFFFFFFF)
            integer(values, f"{sensor}_gen", 0, 0xFFFFFFFF)
            integer(values, f"{sensor}_seq", 0, 255)
        try:
            accuracy = float(values["accuracy_rad"])
        except ValueError as exc:
            raise ProtocolError("Precisão angular inválida") from exc
        if not math.isfinite(accuracy) or accuracy < 0:
            raise ProtocolError("Precisão angular inválida")
        if values["state"] not in {"IDLE", "CALIBRATING", "READY_TO_SAVE", "SAVING"} or values["instruction"] not in INSTRUCTIONS:
            raise ProtocolError("Estado/instrução desconhecido")
        if values["owner"] not in {"TCP", "SERIAL"} or values["dcd"] not in {"NOT_SAVED", "PENDING", "SAVED", "FAILED", "UNKNOWN"}:
            raise ProtocolError("Dono/resultado DCD desconhecido")
        if values["action"] == "START" and prefix == "# OK CAL_IMU" and values["code"] == "ACCEPTED":
            if values["session_id"] == "0" or values["owner"] != "TCP":
                raise ProtocolError("START sem sessão TCP válida")
            self.owned = values["session_id"]
        if self.owned and values["session_id"] != self.owned:
            raise ProtocolError("Sessão mudou durante o procedimento")
        if prefix == "# ERR CAL_IMU":
            print(f"Comando recusado: {values['code']}")
        self.current = values
        self.awaiting_reply = None
        return values

    def operator(self, text, default_positions=6):
        text = text.strip().lower()
        state = self.current
        if text == "sair":
            return False
        if text == "iniciar" or text.startswith("iniciar "):
            if self.owned or state["state"] != "IDLE":
                print("Já existe uma sessão ativa.")
                return True
            parts = text.split()
            faces = parts[1] if len(parts) == 2 else str(default_positions) if len(parts) == 1 else ""
            if faces not in {"4", "5", "6"}:
                print("Use iniciar 4, iniciar 5 ou iniciar 6.")
            else:
                self.exchange(f"CAL_IMU START {faces}")
        elif not self.owned:
            print("Este monitor não iniciou uma sessão. Use iniciar ou sair.")
        elif text == "cancelar":
            self.exchange(f"CAL_IMU CANCEL {self.owned}")
        elif text == "salvar":
            if self.save_attempted:
                print("SAVE já foi enviado; aguarde seu resultado.")
            elif "SAVE" in state["allowed"].split(","):
                self.exchange(f"CAL_IMU SAVE {self.owned}")
            else:
                print("Gravação ainda não permitida. Siga a instrução e confira a qualidade recente.")
        elif text == "":
            if "CONFIRM" in state["allowed"].split(","):
                self.exchange(f"CAL_IMU CONFIRM {self.owned} {state['step_id']}")
            else:
                print("Enter não confirma esta etapa. Siga a instrução atual.")
        else:
            print("Use Enter para confirmar movimento, salvar, cancelar ou sair.")
        return True

    def finish_on_exit(self):
        # Ctrl+C can arrive inside a read. Consume that command's ACK before
        # sending CANCEL, including a START whose session ID is not yet known.
        if self.awaiting_reply:
            self.receive_reply(self.awaiting_reply)
        if not self.owned or self.current["state"] == "IDLE":
            return
        state = self.exchange(f"CAL_IMU CANCEL {self.owned}")
        # One in-flight command plus two restore commands, bounded by firmware.
        deadline = time.monotonic() + 3 * int(state["op_ms"]) / 1000 + 5
        while state["state"] != "IDLE" and time.monotonic() < deadline:
            time.sleep(0.2)
            state = self.exchange("CAL_IMU STATUS")
        print(f"Resultado: {state['result']} | DCD: {state['dcd']} | {state['detail']} | restauração: {state['restore']}")
        if state["state"] != "IDLE":
            raise ProtocolError("Prazo de encerramento excedido; consulte CAL_IMU STATUS ao reconectar")


def failure_lines(s):
    labels = {"READ_ORIGINAL": "ler a configuração original (GET_CAL)",
              "CONFIGURE": "configurar a calibração", "VERIFY": "verificar a configuração",
              "SAVE": "salvar DCD", "RESTORE": "restaurar a configuração",
              "VERIFY_RESTORE": "verificar a restauração"}
    op, rc = s.get("failed_op", "NONE"), s.get("failed_rc", "0")
    if op == "NONE":
        # Existing firmware already records these, even when rc is overwritten by restoration.
        for key, candidate in (("read_rc", "READ_ORIGINAL"), ("config_rc", "CONFIGURE"),
                               ("verify_rc", "VERIFY"), ("save_rc", "SAVE"), ("restore_rc", "RESTORE")):
            if int(s.get(key, "1")) < 0:
                op, rc = candidate, s[key]
                break
    if op == "NONE":
        return []
    reason = {"-6": "timeout aguardando resposta válida", "-4": "erro de comunicação",
              "-5": "erro reportado pelo sensor"}.get(rc, "falha SH-2")
    lines = [f"Falha ao {labels.get(op, op)}: {reason} (rc={rc})."]
    if op == "READ_ORIGINAL" and s.get("config_rc", "1") == "1":
        lines.append("A configuração de calibração não foi alterada; nenhum SAVE foi enviado.")
    if "cmd_rx" in s:
        lines.append(f"Comando {s['cmd']} seq={s['cmd_seq']}: envio rc={s['cmd_tx_rc']}, "
                     f"respostas={s['cmd_rx']}, correspondentes={s['cmd_matched']}, "
                     f"controle={s['cmd_control']}, desconhecidos={s['cmd_unknown']}, truncados={s['cmd_truncated']}.")
    return lines


class Display:
    def __init__(self, journal, plain=False):
        self.journal = journal
        self.ansi = sys.stdout.isatty() and os.getenv("TERM") != "dumb" and not plain
        self.last_step = None
        self.last_result = None
        self.last_print = 0.0
        self.drawn = False
        self.generations = {}

    def show(self, s):
        key = (s["session_id"], s["step_id"], s["instruction"], s["state"], s["phase"])
        changed = key != self.last_step
        result_key = tuple(s.get(k) for k in ("session_id", "result", "detail", "dcd", "restore", "failed_op", "failed_rc"))
        result_changed = s["state"] == "IDLE" and s["result"] != "NONE" and result_key != self.last_result
        if changed:
            self.journal.write("instruction", session_id=s["session_id"], step_id=s["step_id"],
                               instruction=s["instruction"], text=INSTRUCTIONS[s["instruction"]])
            self.last_step = key
        if not self.ansi and not changed and not result_changed and time.monotonic() - self.last_print < 1:
            return
        self.last_print = time.monotonic()
        quality = []
        for sensor, label in (("mag", "Magnetic Field"), ("game", "Game RV"), ("rv", "Rotation Vector")):
            status, age, gen = s[f"{sensor}_status"], int(s[f"{sensor}_age_ms"]), s[f"{sensor}_gen"]
            recency = "ausente" if age < 0 else "desatualizado" if age >= int(s["fresh_ms"]) else "recente"
            novelty = "novo" if self.generations.get(sensor) != gen and age >= 0 else "sem novidade"
            self.generations[sensor] = gen
            quality.append(f"{label}: {status}/3 ({QUALITIES[status]}), {age} ms, {recency}, {novelty}")
        header = f"Sessão {s['session_id']} | {s['state']}/{s['phase']} | etapa {s['stage']}/6 | id {s['step_id']} | posição {s['position']}/{s['positions']} | rodada {s['round']}"
        summary = f"accuracy RV={s['accuracy_rad']} rad | restante={int(s['remaining_ms'])/1000:.0f} s | DCD={s['dcd']}"
        if self.ansi and self.drawn:
            print("\033[5F\033[J", end="")
        if changed:
            print(header + "\n" + INSTRUCTIONS[s["instruction"]])
        if result_changed:
            print(f"Resultado: {s['result']} ({s['detail']}) | DCD={s['dcd']} | restauração={s['restore']}")
            for line in failure_lines(s):
                print(line)
            self.last_result = result_key
        actions = "iniciar [4/5/6]: nova sessão | sair" if s["state"] == "IDLE" else "Enter: confirmar | salvar: Salvar DCD | cancelar | sair"
        rows = [*quality, summary, actions]
        if self.ansi:
            width = max(1, shutil.get_terminal_size().columns - 1)
            print("\n".join(row[:width] for row in rows), flush=True)
            self.drawn = True
        else:
            print(" | ".join([*quality, summary]), flush=True)



def result_code(state):
    if state["result"] == "SAVED" and state["dcd"] == "SAVED" and state["restore"] == "OK":
        return 0
    return 2 if state["result"] == "CANCELLED" else 1


def run_monitor(monitor, journal, positions=6, plain=False, input_fd=None):
    display = Display(journal, plain)
    fd = sys.stdin.fileno() if input_fd is None else input_fd
    buffer = bytearray()
    monitor.handshake()
    state = monitor.exchange("CAL_IMU STATUS") # Opening never starts calibration.
    next_poll = time.monotonic() + 0.2
    display.show(state)
    try:
        while True:
            if monitor.owned and state["state"] == "IDLE":
                return result_code(state)
            ready, _, _ = select.select([fd], [], [], max(0, next_poll-time.monotonic()))
            if ready:
                chunk = os.read(fd, 4096)
                if not chunk:
                    monitor.finish_on_exit()
                    return result_code(monitor.current) if monitor.owned else 0
                buffer.extend(chunk)
                if len(buffer) > 4096:
                    raise ValueError("Entrada do terminal excedeu o limite")
                if b"\n" in buffer:
                    text = bytes(buffer).split(b"\n", 1)[0].decode("utf-8", errors="replace")
                    buffer.clear() # Queued Enter presses must not confirm later, unseen steps.
                    display.drawn = False # The terminal echoed the submitted input line.
                    if not monitor.operator(text, positions):
                        monitor.finish_on_exit()
                        return result_code(monitor.current) if monitor.owned else 0
                    state = monitor.current
                    display.show(state)
            if time.monotonic() >= next_poll:
                state = monitor.exchange("CAL_IMU STATUS")
                display.show(state)
                next_poll = time.monotonic() + 0.2
    except KeyboardInterrupt:
        monitor.finish_on_exit()
        return 130


def main(argv=None):
    parser = argparse.ArgumentParser(description="Calibração manual da IMU via Wi-Fi, sem USB. Abrir não inicia calibração.")
    parser.add_argument("--host", type=ipv4, default=os.getenv("LAB_HOST") or None)
    parser.add_argument("--port", type=port_number, default=os.getenv("LAB_PORT", "5000"))
    parser.add_argument("--timeout", type=timeout_seconds, default=os.getenv("LAB_TIMEOUT", "5"))
    parser.add_argument("--positions", type=int, choices=(4, 5, 6), default=6)
    parser.add_argument("--plain", action="store_true", help="saída textual sem atualização ANSI")
    parser.add_argument("--log", type=Path, default=Path(os.getenv("LAB_CAL_LOG") or f"/data/cal-imu-{time.time_ns()}.jsonl"))
    args = parser.parse_args(argv)
    if args.host is None:
        parser.error("informe --host IP ou configure LAB_HOST")
    journal = None
    monitor = None
    try:
        journal = Journal(args.log) # Fail before any sensor change if logging is unavailable.
        journal.write("connect", host=args.host, port=args.port, positions=args.positions)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(args.timeout)
            sock.connect((args.host, args.port))
            monitor = Monitor(sock, args.timeout, journal)
            print(f"Registro: {args.log}\nQualidade 0–3 não é porcentagem nem garantia de precisão. Cancelar não restaura coeficientes antigos em RAM.")
            return run_monitor(monitor, journal, args.positions, args.plain)
    except (OSError, ValueError, ProtocolError) as exc:
        unknown = bool(monitor and monitor.save_attempted)
        print(f"ERRO: {exc}." + (" Resultado do SAVE pode ser desconhecido; não reenvie. Consulte STATUS ao reconectar." if unknown else " A desconexão encerra a sessão sem iniciar SAVE."), file=sys.stderr)
        if journal:
            try:
                journal.write("error", error=str(exc), save_result_may_be_unknown=unknown)
            except OSError:
                pass
        return 1
    finally:
        if journal:
            journal.close()


if __name__ == "__main__":
    sys.exit(main())
