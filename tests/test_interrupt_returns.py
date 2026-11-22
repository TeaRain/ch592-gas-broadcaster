from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
OBJDUMP = os.environ.get(
    "RISCV_OBJDUMP",
    "/tmp/ch592-toolchain-20261122/root/usr/bin/riscv64-unknown-elf-objdump",
)


class InterruptReturnTests(unittest.TestCase):
    def test_interrupt_handlers_return_with_mret(self):
        for function in ("GPIOB_IRQHandler", "RTC_IRQHandler", "LLE_IRQHandler"):
            elf = REPO / "obj/Broadcaster.elf"
            with self.subTest(elf=elf.name, function=function):
                with self.subTest(elf=elf.name, function=function):
                    result = subprocess.run(
                        [OBJDUMP, "-d", f"--disassemble={function}", str(elf)],
                        check=True,
                        capture_output=True,
                        text=True,
                    )
                    instructions = [
                        parts[-1]
                        for line in result.stdout.splitlines()
                        if (parts := line.split()) and parts[-1] in ("mret", "ret")
                    ]
                    self.assertTrue(instructions, f"no return instruction found in {function}")
                    self.assertEqual(instructions[-1], "mret")


if __name__ == "__main__":
    unittest.main()
