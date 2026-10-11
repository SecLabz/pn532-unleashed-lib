from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]


def capture_levels(program_name, sm_cycles_per_fc, pin):
    """Execute only this RX program's PIO instructions, excluding GPIO sync.

    Record input sampling cycles and leading-edge acceptance. No CPU IRQ,
    FIFO contention or analog edge timing is modeled.
    """
    source = (ROOT / "src/phy/nfca/picc/nfca_rx_levels.pio").read_text()
    section = source.split(".program " + program_name + "\n", 1)[1]
    section = section.split(".program ", 1)[0]
    labels, instructions = {}, []
    for line in section.splitlines():
        line = line.split(";", 1)[0].strip()
        if not line or line.startswith("."):
            continue
        if line.endswith(":"):
            labels[line[:-1]] = len(instructions)
        else:
            instructions.append(line)
    pc, cycle, y = 0, 0, 0
    accepted, samples = [], []
    while cycle < 1000 * sm_cycles_per_fc and len(samples) < 40:
        instruction = instructions[pc]
        delay = re.search(r"\[(\d+)\]", instruction)
        duration = 1 + (int(delay[1]) if delay else 0)
        operation = instruction.split("[", 1)[0].strip()
        next_pc = pc + 1
        if operation.startswith("jmp pin "):
            target = operation.split()[-1]
            if pin(cycle / sm_cycles_per_fc):
                if target.startswith("filter_high_"):
                    accepted.append(cycle)
                next_pc = labels[target]
        elif operation.startswith("jmp y-- "):
            if y != 0:
                next_pc = labels[operation.split()[-1]]
            y = (y - 1) & 0xFFFFFFFF
        elif operation.startswith("jmp "):
            next_pc = labels[operation.split()[-1]]
        elif operation == "set y, 31":
            y = 31
        elif operation == "in pins, 1":
            samples.append(cycle)
        elif operation != "nop":
            raise AssertionError("Unmodeled PIO instruction: " + instruction)
        cycle += duration
        pc = next_pc
    return accepted, samples


class TimingProfileTests(unittest.TestCase):
    def test_compiled_clock_and_fdt_units_agree_with_trace_header(self):
        output = ROOT / ".cache/tests/timing"
        output.mkdir(parents=True, exist_ok=True)
        for clock, expected in (
            (135600000, "135600000 80 1280 9960 9320 2.0"),
            (54240000, "54240000 32 512 3984 3728 1.0"),
        ):
            with self.subTest(clock=clock):
                executable = output / str(clock)
                subprocess.run(
                    [
                        "cc",
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-DPNU_RF_TRACE=1",
                        f"-DPNU_REQUIRED_SYS_CLOCK_HZ={clock}u",
                        "-I" + str(ROOT / "include"),
                        "-I" + str(ROOT / "src"),
                        str(ROOT / "tests/phy/timing_bridge.c"),
                        str(ROOT / "src/phy/nfca/picc/nfca_trace.c"),
                        "-o",
                        str(executable),
                    ],
                    check=True,
                )
                self.assertEqual(
                    subprocess.check_output(
                        [str(executable)], text=True
                    ).strip(),
                    expected,
                )

    def test_pio_profiles_preserve_first_level_phase_and_sampling_grid(self):
        for ratio in (5, 4):
            with self.subTest(ratio=ratio):
                accepted, samples = capture_levels(
                    f"nfca_rx_levels_{ratio}fc", ratio, lambda fc: fc >= 40
                )
                self.assertGreater(len(samples), 32)
                self.assertEqual(samples[0] - accepted[0], 20 * ratio)
                self.assertEqual(
                    {b - a for a, b in zip(samples, samples[1:])}, {8 * ratio}
                )

    def test_pio_profiles_reject_short_envelope_before_valid_sof(self):
        for ratio in (5, 4):
            with self.subTest(ratio=ratio):
                accepted, samples = capture_levels(
                    f"nfca_rx_levels_{ratio}fc",
                    ratio,
                    lambda fc: 40 <= fc < 46 or fc >= 80,
                )
                self.assertEqual(len(accepted), 2)
                self.assertGreaterEqual(samples[0], 100 * ratio)
                self.assertEqual(samples[0] - accepted[1], 20 * ratio)
