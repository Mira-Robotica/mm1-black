"""Protocolo de orientação do PFC (numeração de novembro de 2026)."""
import os
import select
import sys
import termios
import time


def build_targets():
    j7 = 270
    targets = [(0, 0, 0, 0, 0, 90, j7)]
    for _cycle in range(3):
        for _roll in range(4):
            for j6 in (120, 90, 60, 30, 0):
                for j5 in range(0, 360, 45):
                    targets.append((0, 0, 0, 0, j5, j6, j7))
            j7 += 90
            targets.append((0, 0, 0, 0, 0, 90, j7))
    return targets


def pose_fields(number, index, target):
    cycle = (max(1, index) - 1) // 164 + 1
    condition = ("sem_ima", "com_ima", "apos_retirada")[cycle - 1] if number == 3 else "sem_ima"
    return dict(experiment_number=number, cycle=cycle, magnetic_condition=condition,
                j7_accumulated_deg=target[6], j7_normalized_deg=target[6] % 360)


def wait_for_enter(action, check):
    # roslaunch starts the node in a new session, but can pass its terminal fd.
    if sys.stdin.isatty():
        terminal = os.fdopen(os.dup(sys.stdin.fileno()), "rb", buffering=0)
    else:
        terminal = open("/dev/tty", "rb", buffering=0)
    with terminal:
        termios.tcflush(terminal, termios.TCIFLUSH)
        print(action + " e pressione Enter para continuar.", flush=True)
        while True:
            check()
            if select.select([terminal], [], [], 0.2)[0]:
                line = terminal.readline()
                if not line:
                    raise RuntimeError("Terminal fechado durante a pausa")
                if line in (b"\n", b"\r\n"):
                    return


def pause_after_cycle(index, target, robot, write):
    action = {164: "Posicione o ímã no tripé", 328: "Retire o ímã do tripé"}.get(index)
    if action is None:
        return
    cycle = index // 164
    with robot.capture_guard(target):
        started = time.monotonic()
        write("pause_start", after_cycle=cycle, action=action)
        wait_for_enter(action, robot.snapshot)
        write("pause_confirmed", after_cycle=cycle, action=action,
              duration_s=time.monotonic() - started)
